package com.coldstart.game;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.Semaphore;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class ColdStartActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "SDL2_mixer",
            "main"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        applyImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyImmersiveMode();
    }

    // Called from native code via JNI to perform an HTTP GET and return the body bytes.
    // Returns null on failure (non-2xx, network error, etc).
    public static byte[] httpFetchBytes(String urlStr, int timeoutMs) {
        try {
            URL u = new URL(urlStr);
            HttpURLConnection conn = (HttpURLConnection) u.openConnection();
            conn.setConnectTimeout(timeoutMs);
            conn.setReadTimeout(timeoutMs);
            conn.setInstanceFollowRedirects(true);
            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) return null;
            InputStream is = conn.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) != -1) out.write(buf, 0, n);
            is.close();
            return out.toByteArray();
        } catch (Exception e) { return null; }
    }

    // Called from native code via JNI to download a URL to a local file path.
    public static boolean httpFetchFile(String urlStr, String destPath, int timeoutMs) {
        try {
            URL u = new URL(urlStr);
            HttpURLConnection conn = (HttpURLConnection) u.openConnection();
            conn.setConnectTimeout(timeoutMs);
            conn.setReadTimeout(timeoutMs);
            conn.setInstanceFollowRedirects(true);
            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) return false;
            InputStream is = conn.getInputStream();
            FileOutputStream fos = new FileOutputStream(destPath);
            byte[] buf = new byte[65536];
            int n;
            while ((n = is.read(buf)) != -1) fos.write(buf, 0, n);
            is.close();
            fos.close();
            return true;
        } catch (Exception e) { return false; }
    }

    // Called from native code via JNI to extract a zip archive into destDir.
    // Flattens a single top-level folder automatically (matches PC/Switch behaviour).
    public static boolean extractZip(String zipPath, String destDir) {
        try {
            File dest = new File(destDir);
            dest.mkdirs();

            ZipInputStream zis = new ZipInputStream(new FileInputStream(zipPath));
            ZipEntry entry;
            byte[] buf = new byte[65536];

            // First pass: count top-level entries to detect single-folder zips
            java.util.List<String> topLevelDirs = new java.util.ArrayList<>();
            java.util.List<ZipEntry> allEntries = new java.util.ArrayList<>();
            while ((entry = zis.getNextEntry()) != null) {
                allEntries.add(entry);
                String name = entry.getName().replace("\\", "/");
                if (name.contains("/")) {
                    String top = name.substring(0, name.indexOf('/'));
                    if (!topLevelDirs.contains(top)) topLevelDirs.add(top);
                }
                zis.closeEntry();
            }
            zis.close();

            // Single top-level folder? Strip it when extracting.
            String stripPrefix = (topLevelDirs.size() == 1) ? (topLevelDirs.get(0) + "/") : "";

            // Second pass: extract
            zis = new ZipInputStream(new FileInputStream(zipPath));
            while ((entry = zis.getNextEntry()) != null) {
                String name = entry.getName().replace("\\", "/");
                if (!stripPrefix.isEmpty() && name.startsWith(stripPrefix))
                    name = name.substring(stripPrefix.length());
                if (name.isEmpty()) { zis.closeEntry(); continue; }

                // Security: reject path traversal
                File outFile = new File(dest, name);
                if (!outFile.getCanonicalPath().startsWith(dest.getCanonicalPath() + File.separator)
                        && !outFile.getCanonicalPath().equals(dest.getCanonicalPath())) {
                    zis.closeEntry(); continue;
                }

                if (entry.isDirectory()) {
                    outFile.mkdirs();
                } else {
                    if (outFile.getParentFile() != null) outFile.getParentFile().mkdirs();
                    FileOutputStream fos = new FileOutputStream(outFile);
                    int n;
                    while ((n = zis.read(buf)) != -1) fos.write(buf, 0, n);
                    fos.close();
                }
                zis.closeEntry();
            }
            zis.close();
            return true;
        } catch (Exception e) { return false; }
    }

    // ── Storage permission + SAF folder picker ───────────────────────────────
    // Called from JNI: requests MANAGE_EXTERNAL_STORAGE (Android 11+) or
    // READ/WRITE_EXTERNAL_STORAGE (Android <= 10), then opens SAF picker.
    // Blocks until permission flow and folder pick are both complete.
    // Returns filesystem path, or "" on failure/cancel.

    private static final int REQUEST_LEGACY_STORAGE = 9002;
    private static Semaphore s_permissionSemaphore = null;

    public String requestStorageAndBrowse() {
        // Android 11+: need MANAGE_EXTERNAL_STORAGE for arbitrary path fopen()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                s_permissionSemaphore = new Semaphore(0);
                runOnUiThread(() -> {
                    try {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + getPackageName()));
                        startActivityForResult(intent, REQUEST_LEGACY_STORAGE);
                    } catch (Exception e) {
                        Semaphore sem = s_permissionSemaphore;
                        if (sem != null) sem.release();
                    }
                });
                try { s_permissionSemaphore.acquire(); } catch (InterruptedException ignored) {}
                s_permissionSemaphore = null;
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED) {
                s_permissionSemaphore = new Semaphore(0);
                requestPermissions(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQUEST_LEGACY_STORAGE);
                try { s_permissionSemaphore.acquire(); } catch (InterruptedException ignored) {}
                s_permissionSemaphore = null;
            }
        }
        return browseFolder();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_LEGACY_STORAGE) {
            Semaphore sem = s_permissionSemaphore;
            if (sem != null) sem.release();
        }
    }

    // ── Android Storage Access Framework folder picker ────────────────────────
    // Called from native JNI on the SDL main thread; blocks until the user
    // picks a folder (or cancels).  Returns a filesystem path, or null on
    // failure/cancel or when the URI cannot be mapped to a real path.

    private static final int REQUEST_PICK_FOLDER = 9001;
    private static volatile String s_pickedFolderPath = null;
    private static Semaphore s_folderPickSemaphore = null;

    public String browseFolder() {
        s_pickedFolderPath = null;
        s_folderPickSemaphore = new Semaphore(0);

        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(
                    Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                startActivityForResult(intent, REQUEST_PICK_FOLDER);
            } catch (Exception e) {
                Semaphore sem = s_folderPickSemaphore;
                if (sem != null) sem.release();
            }
        });

        try { s_folderPickSemaphore.acquire(); } catch (InterruptedException ignored) {}
        return s_pickedFolderPath;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_LEGACY_STORAGE) {
            Semaphore sem = s_permissionSemaphore;
            if (sem != null) sem.release();
            return;
        }
        if (requestCode == REQUEST_PICK_FOLDER) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                Uri uri = data.getData();
                if (uri != null) {
                    try {
                        getContentResolver().takePersistableUriPermission(uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION |
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                    } catch (Exception ignored) {}
                    s_pickedFolderPath = treeUriToPath(uri);
                }
            }
            Semaphore sem = s_folderPickSemaphore;
            s_folderPickSemaphore = null;
            if (sem != null) sem.release();
        }
    }

    // Convert a DocumentTree URI to a best-effort filesystem path.
    // Works reliably for the primary emulated storage (most devices).
    // For SD card volumes, attempts /storage/<volume>/<path>.
    private String treeUriToPath(Uri treeUri) {
        try {
            String docId = DocumentsContract.getTreeDocumentId(treeUri);
            if (docId == null) return null;
            String[] parts = docId.split(":", 2);
            if (parts.length < 2) return null;
            String volume = parts[0];
            String relative = parts[1];
            if ("primary".equalsIgnoreCase(volume)) {
                String base = Environment.getExternalStorageDirectory().getAbsolutePath();
                return relative.isEmpty() ? base : (base + "/" + relative);
            }
            // Non-primary (SD card): try /storage/<volume-id>/
            File sdBase = new File("/storage/" + volume);
            if (sdBase.exists() && sdBase.canRead()) {
                return relative.isEmpty() ? sdBase.getAbsolutePath()
                                          : (sdBase.getAbsolutePath() + "/" + relative);
            }
        } catch (Exception ignored) {}
        return null;
    }

    private void applyImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
            );
        }
    }
}
