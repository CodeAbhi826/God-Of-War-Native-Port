#include "ps2_runtime.h"

void ps2InstallEeDiff(PS2Runtime &runtime); // src/lib/ee_ref/ee_diff.cpp
void ps2InstallEeWatch(PS2Runtime &runtime);
void ps2InstallEePageWatch(PS2Runtime &runtime);
#include "games_database.h"
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
#include "ps2_debug_panel.h"
#endif

#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <iostream>
#include <string>
#include <filesystem>
#include <exception>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <optional>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <unistd.h>
#include <thread>
#include <cstdio>
#include <cstring>

#include "ps2x/iop/disc_source.h"

void ps2AndroidVkProbe();
#endif

namespace
{
#if defined(__ANDROID__)
    int g_logcatPipeFds[2]{-1, -1};
    std::thread g_logcatThread;

    void stopLogcatRedirect()
    {
        std::fflush(stdout);
        std::fflush(stderr);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        if (g_logcatPipeFds[1] >= 0)
        {
            close(g_logcatPipeFds[1]);
            g_logcatPipeFds[1] = -1;
        }
        if (g_logcatThread.joinable())
        {
            g_logcatThread.join();
        }
    }

    void redirectStdioToLogcat()
    {
        if (pipe(g_logcatPipeFds) != 0)
        {
            return;
        }

        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(g_logcatPipeFds[1], STDOUT_FILENO);
        dup2(g_logcatPipeFds[1], STDERR_FILENO);

        g_logcatThread = std::thread([]()
                                     {
                                         FILE *reader = fdopen(g_logcatPipeFds[0], "r");
                                         if (!reader)
                                         {
                                             return;
                                         }
                                         char line[1024];
                                         while (fgets(line, sizeof(line), reader))
                                         {
                                             size_t len = std::strlen(line);
                                             if (len > 0 && line[len - 1] == '\n')
                                             {
                                                 line[len - 1] = '\0';
                                             }
                                             __android_log_write(ANDROID_LOG_INFO, "ps2x", line);
                                         }
                                         fclose(reader);
                                         g_logcatPipeFds[0] = -1;
                                     });
        if (std::atexit(stopLogcatRedirect) != 0)
        {
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            close(g_logcatPipeFds[1]);
            g_logcatPipeFds[1] = -1;
            if (g_logcatThread.joinable())
            {
                g_logcatThread.join();
            }
        }
    }
#endif

    void setupTerminateLogger() // to help on release build crashs
    {
        std::set_terminate([]()
                           {
                               std::cerr << "[terminate] unhandled exception" << std::endl;
                               const std::exception_ptr ep = std::current_exception();
                               if (ep)
                               {
                                   try
                                   {
                                       std::rethrow_exception(ep);
                                   }
                                   catch (const std::system_error &e)
                                   {
                                       std::cerr << "[terminate] std::system_error code=" << e.code().value()
                                                 << " category=" << e.code().category().name()
                                                 << " message=" << e.what() << std::endl;
                                   }
                                   catch (const std::exception &e)
                                   {
                                       std::cerr << "[terminate] std::exception: " << e.what() << std::endl;
                                   }
                                   catch (...)
                                   {
                                       std::cerr << "[terminate] non-std exception" << std::endl;
                                   }
                               }
                               std::abort(); });
    }

    std::string normalizeGameId(const std::string &folderName)
    {
        std::string result = folderName;

        size_t underscore = result.find('_');
        if (underscore != std::string::npos)
            result[underscore] = '-';

        size_t dot = result.find('.');
        if (dot != std::string::npos)
            result.erase(dot, 1);

        std::ranges::transform(result, result.begin(), [](unsigned char character)
                               { return static_cast<char>(std::toupper(character)); });

        return result;
    }

    std::filesystem::path getExecutablePath(int argc, char *argv[])
    {
        if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        {
            std::cout << "Using argv boot path" << std::endl;
            return std::filesystem::path(argv[1]);
        }
#if defined(PS2X_DEFAULT_BOOT_ELF)
        std::cout << "Using default boot file" << std::endl;
        const std::filesystem::path configuredPath = std::filesystem::path(PS2X_DEFAULT_BOOT_ELF);
#if defined(PLATFORM_VITA)
        return configuredPath;
#endif
        if (configuredPath.is_absolute())
        {
            return configuredPath;
        }
        return (std::filesystem::current_path() / configuredPath).lexically_normal();
#else
        throw std::runtime_error("Unable to determine executable path. Pass the guest ELF as argv[1] or define PS2X_DEFAULT_BOOT_ELF.");
#endif
    }

#if defined(__ANDROID__)
    constexpr const char *kAndroidGameConfigName = "game_paths.txt";

    struct AndroidGamePaths
    {
        std::string isoPath;
        std::filesystem::path bootElf;
    };

    // MainActivity writes this file after the user picks an ISO. The descriptor number is valid because both
    // activities live in the same process and the launcher keeps the ParcelFileDescriptor open.
    bool readAndroidGamePaths(AndroidGamePaths &out)
    {
#if defined(PS2X_DEFAULT_BOOT_ELF)
        const std::filesystem::path configPath =
            std::filesystem::path(PS2X_DEFAULT_BOOT_ELF).parent_path() / kAndroidGameConfigName;
#else
        const std::filesystem::path configPath =
            std::filesystem::path("/data/user/0/com.ps2x.runner/files") / kAndroidGameConfigName;
#endif
        std::ifstream config(configPath);
        if (!config)
            return false;

        int isoFd = -1;
        std::filesystem::path bootElf;
        std::string line;
        while (std::getline(config, line))
        {
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const std::string key = line.substr(0, separator);
            const std::string value = line.substr(separator + 1);
            if (key == "iso_fd")
                isoFd = std::atoi(value.c_str());
            else if (key == "boot_elf")
                bootElf = std::filesystem::u8path(value);
        }
        if (isoFd < 0 || bootElf.empty())
        {
            std::cerr << "[android] invalid disc config " << configPath << std::endl;
            return false;
        }

        out.isoPath = "/proc/self/fd/" + std::to_string(isoFd);
        out.bootElf = std::move(bootElf);
        std::cout << "[android] disc config " << configPath << " (fd " << isoFd << ")" << std::endl;
        return true;
    }

    // Extracts only the guest ELF from the user's ISO (about 2 MB); the ISO itself stays in place and is
    // handed to the CDVD layer through the descriptor path.
    std::string extractAndroidBootElf(const std::string &isoPath, const std::filesystem::path &bootElf)
    {
        auto disc = ps2x::iop::DiscSource::openImage(std::filesystem::u8path(isoPath));
        if (!disc)
        {
            std::cerr << "[android] cannot open ISO9660 image " << isoPath << std::endl;
            return {};
        }

        std::vector<std::string> candidates;
#if defined(PS2X_GAME_SERIAL)
        candidates.emplace_back(std::string("cdrom0:\\") + PS2X_GAME_SERIAL);
#else
        candidates.emplace_back("cdrom0:\\SCES_531.33");
        candidates.emplace_back("cdrom0:\\SCUS_973.99");
#endif

        std::optional<ps2x::iop::DiscSource::File> found;
        for (const std::string &candidate : candidates)
        {
            found = disc->findFile(candidate);
            if (found)
                break;
        }
        if (!found)
        {
            std::cerr << "[android] no supported God of War ELF in " << isoPath << std::endl;
            return {};
        }

        const std::size_t sectorSize = ps2x::iop::DiscSource::kSectorSize;
        const std::size_t sectors = (static_cast<std::size_t>(found->size) + sectorSize - 1u) / sectorSize;
        std::vector<std::uint8_t> bytes(sectors * sectorSize);
        if (!disc->readSectors(found->lsn, static_cast<std::uint32_t>(sectors), bytes.data()))
        {
            std::cerr << "[android] failed reading " << found->name << " from " << isoPath << std::endl;
            return {};
        }

        const std::filesystem::path temporary = bootElf.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                std::cerr << "[android] cannot write " << temporary << std::endl;
                return {};
            }
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(found->size));
            if (!output)
            {
                std::cerr << "[android] failed writing " << temporary << std::endl;
                return {};
            }
        }

        std::error_code error;
        std::filesystem::rename(temporary, bootElf, error);
        if (error)
        {
            std::cerr << "[android] cannot install " << bootElf << ": " << error.message() << std::endl;
            return {};
        }

        std::cout << "[android] extracted " << found->name << " (" << found->size << " bytes) to "
                  << bootElf << std::endl;
        return found->name;
    }
#endif
}

int main(int argc, char *argv[])
{
#if defined(__ANDROID__)
    redirectStdioToLogcat();
    ps2AndroidVkProbe();
#endif
    setupTerminateLogger();

    try
    {
        std::filesystem::path pathObj;
        std::string elfName;
        bool hasIsoPaths = false;
        PS2Runtime::IoPaths isoIoPaths;

#if defined(__ANDROID__)
        AndroidGamePaths androidPaths;
        if (readAndroidGamePaths(androidPaths))
        {
            const std::string extracted = extractAndroidBootElf(androidPaths.isoPath, androidPaths.bootElf);
            if (!extracted.empty())
            {
                pathObj = androidPaths.bootElf;
                elfName = extracted;
                isoIoPaths.elfPath = pathObj;
                isoIoPaths.elfDirectory = pathObj.parent_path();
                isoIoPaths.hostRoot = isoIoPaths.elfDirectory;
                isoIoPaths.cdRoot = isoIoPaths.elfDirectory;
                isoIoPaths.mcRoot = isoIoPaths.elfDirectory / "mc0";
                isoIoPaths.cdImage = androidPaths.isoPath;
                hasIsoPaths = true;
            }
        }
        if (!hasIsoPaths)
        {
            pathObj = getExecutablePath(argc, argv);
            elfName = pathObj.filename().string();
        }
#else
        pathObj = getExecutablePath(argc, argv);
        elfName = pathObj.filename().string();
#endif

        std::string filePathStr = pathObj.string();
        std::string normalizedId = normalizeGameId(elfName);

        std::string windowTitle = "PS2-Recomp | ";
        const char *gameName = getGameName(normalizedId);

#if !defined(PLATFORM_VITA)
        if (gameName)
        {
            // GOW-Port: a known game's window carries only its name, like a native PC release
            windowTitle = gameName;
        }
        else
#endif
        {
            windowTitle += elfName;
        }

        PS2Runtime runtime;
        if (hasIsoPaths)
        {
            PS2Runtime::setIoPaths(isoIoPaths);
        }
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
        // This hook is to prevent leak rlimgui deps to recompiler etc
        PS2DebugPanel debugPanel;
        runtime.setDebugUiCallbacks(
            [](PS2Runtime &rt, void *userData)
            {
                (void)rt;
                static_cast<PS2DebugPanel *>(userData)->initialize();
            },
            [](PS2Runtime &rt, void *userData)
            {
                static_cast<PS2DebugPanel *>(userData)->draw(rt);
            },
            [](PS2Runtime &rt, void *userData)
            {
                (void)rt;
                static_cast<PS2DebugPanel *>(userData)->shutdown();
            },
            &debugPanel);
#endif
        if (!runtime.initialize(windowTitle.c_str()))
        {
            std::cerr << "Failed to initialize PS2 runtime" << std::endl;
            return 1;
        }

        if (!runtime.loadELF(filePathStr))
        {
            std::cerr << "Failed to load ELF file: " << filePathStr << std::endl;
            return 1;
        }

        ps2InstallEeDiff(runtime);
        ps2InstallEeWatch(runtime);
        ps2InstallEePageWatch(runtime);
        runtime.run();

#ifdef _DEBUG
        ps2_log::print_saved_location();
#endif
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[main] fatal exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[main] fatal exception: unknown" << std::endl;
    }

    std::cout.flush();
    std::cerr.flush();
    std::_Exit(1);
}
