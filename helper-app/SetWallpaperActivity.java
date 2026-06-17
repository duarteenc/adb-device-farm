package com.genfarmer.wallpaperhelper;

import android.app.Activity;
import android.app.WallpaperManager;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import java.io.File;
import java.io.FileInputStream;

public class SetWallpaperActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            String filePath = getIntent().getStringExtra("file");
            if (filePath == null || filePath.isEmpty()) {
                finish();
                return;
            }

            File file = new File(filePath);
            if (!file.exists()) {
                finish();
                return;
            }

            WallpaperManager wallpaperManager = WallpaperManager.getInstance(this);
            FileInputStream fis = new FileInputStream(file);
            wallpaperManager.setStream(fis);
            fis.close();

        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            finish();
        }
    }
}
