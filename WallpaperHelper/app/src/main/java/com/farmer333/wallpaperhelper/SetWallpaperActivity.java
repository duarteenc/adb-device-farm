package com.farmer333.wallpaperhelper;

import android.app.Activity;
import android.app.WallpaperManager;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class SetWallpaperActivity extends Activity {
    private static final String TAG = "333FarmerWallpaper";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String filePath = null;

        // Get file path from intent extra
        if (getIntent() != null && getIntent().hasExtra("file")) {
            filePath = getIntent().getStringExtra("file");
        }

        if (filePath == null || filePath.isEmpty()) {
            Log.e(TAG, "No file path provided in intent");
            finish();
            return;
        }

        Log.i(TAG, "Attempting to set wallpaper: " + filePath);

        File wallpaperFile = new File(filePath);
        if (!wallpaperFile.exists()) {
            Log.e(TAG, "File does not exist: " + filePath);
            finish();
            return;
        }

        try {
            // First copy file to app's private directory (no permissions needed)
            File privateFile = new File(getExternalFilesDir(null), "temp_wallpaper.png");

            FileInputStream fis = new FileInputStream(wallpaperFile);
            FileOutputStream fos = new FileOutputStream(privateFile);
            byte[] buffer = new byte[8192];
            int bytesRead;
            while ((bytesRead = fis.read(buffer)) != -1) {
                fos.write(buffer, 0, bytesRead);
            }
            fis.close();
            fos.close();

            // Now set wallpaper from private file
            WallpaperManager wallpaperManager = WallpaperManager.getInstance(getApplicationContext());
            InputStream inputStream = new FileInputStream(privateFile);
            wallpaperManager.setStream(inputStream);
            inputStream.close();

            // Clean up
            privateFile.delete();

            Log.i(TAG, "Wallpaper set successfully!");

        } catch (IOException e) {
            Log.e(TAG, "Failed to set wallpaper", e);
        } catch (Exception e) {
            Log.e(TAG, "Unexpected error setting wallpaper", e);
        } finally {
            finish();
        }
    }
}
