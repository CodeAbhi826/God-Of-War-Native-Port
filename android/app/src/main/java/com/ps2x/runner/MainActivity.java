package com.ps2x.runner;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

/**
 * Disc picker for the native runner.
 *
 * The APK intentionally carries no game data. The user selects their own
 * God of War ISO; we keep the SAF file descriptor open and hand its number to
 * the native side, which extracts the guest ELF and feeds the ISO to the
 * PS2 CDVD layer.
 */
public final class MainActivity extends Activity {
    private static final int REQUEST_ISO = 1001;

    // Kept open for GameActivity, which runs in the same process. The native
    // side opens /proc/self/fd/<fd> and keeps its own descriptor.
    private static ParcelFileDescriptor sIsoFd;

    private TextView status;
    private Button play;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (sIsoFd == null) {
            File stale = new File(filesDir(), "game_paths.txt");
            if (stale.exists()) {
                // The fd from a previous process is gone.
                //noinspection ResultOfMethodCallIgnored
                stale.delete();
            }
        }

        final int pad = (int) (24 * getResources().getDisplayMetrics().density);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(0xFF101014);

        TextView title = new TextView(this);
        title.setText("God of War");
        title.setTextColor(0xFFF2F2F2);
        title.setTextSize(30);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Native recompilation \u00b7 Android / Vulkan\n\n"
                + "Select your own God of War disc image (.iso).\n"
                + "This app contains no game data.");
        subtitle.setTextColor(0xFFB8B8C0);
        subtitle.setTextSize(15);
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, pad / 2, 0, pad);
        root.addView(subtitle);

        status = new TextView(this);
        status.setText("No disc selected");
        status.setTextColor(0xFFFFC857);
        status.setTextSize(15);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, 0, 0, pad);
        root.addView(status);

        Button select = new Button(this);
        select.setText("Select ISO");
        select.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                pickIso();
            }
        });
        root.addView(select);

        play = new Button(this);
        play.setText("Play");
        play.setEnabled(false);
        play.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startGame();
            }
        });
        root.addView(play);

        TextView note = new TextView(this);
        note.setText("You must own the disc. The ISO stays where you picked it; "
                + "it is never copied into the APK.");
        note.setTextColor(0xFF808088);
        note.setTextSize(12);
        note.setGravity(Gravity.CENTER);
        note.setPadding(0, pad, 0, 0);
        root.addView(note);

        TextView version = new TextView(this);
        version.setText("v" + BuildConfig.VERSION_NAME + " \u00b7 " + BuildConfig.BUILD_SHA);
        version.setTextColor(0xFF606068);
        version.setTextSize(11);
        version.setGravity(Gravity.CENTER);
        version.setPadding(0, pad / 2, 0, 0);
        root.addView(version);

        setContentView(root);
    }

    private File filesDir() {
        File external = getExternalFilesDir(null);
        return external != null ? external : getFilesDir();
    }

    private void pickIso() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES,
                new String[]{"application/octet-stream", "application/x-iso9660-image"});
        startActivityForResult(intent, REQUEST_ISO);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_ISO || resultCode != RESULT_OK
                || data == null || data.getData() == null) {
            return;
        }

        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // The fd still works for this session.
        }

        ParcelFileDescriptor fd = null;
        try {
            fd = getContentResolver().openFileDescriptor(uri, "r");
            if (fd == null) {
                status.setText("Could not open the selected file");
                return;
            }
            if (!hasIsoPvd(fd)) {
                status.setText("Not a PS2 ISO (no ISO9660 volume descriptor)");
                closeQuietly(fd);
                return;
            }

            if (sIsoFd != null) {
                closeQuietly(sIsoFd);
            }
            sIsoFd = fd;
            writeConfig(fd.getFd());

            String name = uri.getLastPathSegment();
            status.setText("Selected: " + (name != null ? name : "ISO")
                    + "\nDisc descriptor: fd " + fd.getFd());
            play.setEnabled(true);
        } catch (Exception e) {
            status.setText("Could not open the ISO: " + e.getMessage());
            play.setEnabled(false);
            closeQuietly(fd);
        }
    }

    /**
     * Reads the ISO9660 primary volume descriptor without touching the shared
     * file offset and without closing the descriptor.
     */
    private boolean hasIsoPvd(ParcelFileDescriptor fd) {
        byte[] pvd = new byte[2048];
        try {
            int read = Os.pread(fd.getFileDescriptor(), pvd, 0, pvd.length, 16L * 2048L);
            return read == pvd.length
                    && pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0'
                    && pvd[4] == '0' && pvd[5] == '1';
        } catch (ErrnoException | IOException e) {
            return false;
        }
    }

    private void writeConfig(int fd) throws IOException {
        File dir = filesDir();
        //noinspection ResultOfMethodCallIgnored
        new File(dir, "mc0").mkdirs();

        File config = new File(dir, "game_paths.txt");
        File bootElf = new File(dir, "boot.elf");
        try (FileWriter writer = new FileWriter(config, false)) {
            writer.write("iso_fd=" + fd + "\n");
            writer.write("boot_elf=" + bootElf.getAbsolutePath() + "\n");
        }
    }

    private void startGame() {
        if (sIsoFd == null) {
            return;
        }
        startActivity(new Intent(this, GameActivity.class));
    }

    private static void closeQuietly(ParcelFileDescriptor fd) {
        if (fd == null) {
            return;
        }
        try {
            fd.close();
        } catch (IOException ignored) {
        }
    }
}
