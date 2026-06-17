package com.genfarmer.wallpaperhelper;

import android.app.Activity;
import android.app.WallpaperManager;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

public class SetWallpaperActivity extends Activity {
    private static final String TAG = "WallpaperHelper";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            // Get file path from intent
            String filePath = null;

            if (getIntent().hasExtra("file")) {
                filePath = getIntent().getStringExtra("file");
            } else if (getIntent().getData() != null) {
                Uri uri = getIntent().getData();
                filePath = uri.getPath();
            }

            if (filePath == null || filePath.isEmpty()) {
                Log.e(TAG, "No file path provided");
                finish();
                return;
            }

            File file = new File(filePath);
            if (!file.exists()) {
                Log.e(TAG, "File does not exist: " + filePath);
                finish();
                return;
            }

            // Set wallpaper
            WallpaperManager wallpaperManager = WallpaperManager.getInstance(getApplicationContext());
            InputStream inputStream = new FileInputStream(file);
            wallpaperManager.setStream(inputStream);
            inputStream.close();

            Log.i(TAG, "Wallpaper set successfully: " + filePath);

        } catch (Exception e) {
            Log.e(TAG, "Error setting wallpaper", e);
        } finally {
            finish();
        }
    }
}
