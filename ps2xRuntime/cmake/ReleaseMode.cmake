include(CheckIPOSupported)

# GOW-Port: the desktop reference build also has to run on pre-AVX2 CPUs (the
# Celeron N4000 test machine has no AVX at all). Turning this on drops
# /arch:AVX2, /GL and /LTCG from MSVC Release builds; the code only uses SSE2
# intrinsics, so nothing is lost functionally.
option(PS2X_DESKTOP_PORTABLE "Avoid AVX2 codegen and LTO in MSVC Release builds" OFF)

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

function(EnableFastReleaseMode TargetName)
    message("> Enabling optimization for: ${TargetName}")
    if(MSVC)
        target_compile_options(${TargetName} PRIVATE
            $<$<CONFIG:Release>:
                /O2 # speed
                /Ob2 # inline aggressively
                /Oi # intrinsics
                /Gy # function-level linking
                /Gw # global data in COMDAT
                /GF # string pooling
                /Zc:inline # remove unreferenced inline
                /fp:fast # fast math (graphics friendly)
                /DNDEBUG
                /GS- # Disable Buffer Security Check (faster)
                /Qspectre- # Disable Spectre mitigations (faster)
            >
        )

        if(NOT PS2X_DESKTOP_PORTABLE)
            target_compile_options(${TargetName} PRIVATE
                $<$<CONFIG:Release>:
                    /GL # whole program opt
                    /arch:AVX2 # Advanced Vector Extensions 2
                >
            )

            if(TARGET ${TargetName})
                target_link_options(${TargetName} PRIVATE
                    $<$<CONFIG:Release>:
                        /LTCG # link-time code generation
                        /OPT:REF # remove unreferenced
                        /OPT:ICF # fold identical COMDATs
                    >
                )
            endif()
        endif()
    endif()

    if(IPO_SUPPORTED AND NOT PS2X_DESKTOP_PORTABLE)
        set_property(TARGET ${TargetName} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    elseif(NOT IPO_SUPPORTED)
        message(WARNING "Interprocedural optimization not supported: ${ipo_error}")
    endif()
endfunction()
