package com.pony.coursetable;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

/**
 * 自己发起系统文件选择器（SAF）。
 *
 * 为什么不用 Qt 的 FileDialog：它在安卓上把选择界面弹出来了，
 * 但结果回不来（QML 里 selectedFile 一直是空的），没法用。
 * 这里自己 startActivityForResult，选完把文件拷进 app 的 cache，
 * 再把路径清单写到 cache/picked.txt —— Qt 那侧 app 回到前台时来取。
 */
public class Picker {

    static final int REQ = 4242;
    static final String RESULT_FILE = "picked.txt";

    public static void start(Activity a, boolean image, boolean multiple) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType(image ? "image/*" : "*/*");
        if (multiple)
            i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        a.startActivityForResult(i, REQ);
    }

    public static void handleResult(Context ctx, int requestCode, int resultCode, Intent data) {
        ArrayList<String> out = new ArrayList<String>();

        if (requestCode == REQ && resultCode == Activity.RESULT_OK && data != null) {
            File dir = new File(ctx.getCacheDir(), "picked");
            dir.mkdirs();
            File[] olds = dir.listFiles();
            if (olds != null)
                for (File f : olds)
                    f.delete();

            ArrayList<Uri> uris = new ArrayList<Uri>();
            Uri single = data.getData();
            if (single != null)
                uris.add(single);
            if (data.getClipData() != null) {
                for (int k = 0; k < data.getClipData().getItemCount(); k++) {
                    Uri u = data.getClipData().getItemAt(k).getUri();
                    if (u != null)
                        uris.add(u);
                }
            }
            for (Uri u : uris) {
                String p = copyTo(ctx, u, dir);
                if (p != null)
                    out.add(p);
            }
        }
        writeResult(ctx, out);
    }

    static String copyTo(Context ctx, Uri uri, File dir) {
        try {
            String name = nameOf(ctx, uri);
            if (name == null || name.isEmpty())
                name = "file";
            File dst = new File(dir, name);
            InputStream in = ctx.getContentResolver().openInputStream(uri);
            if (in == null)
                return null;
            OutputStream os = new FileOutputStream(dst);
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0)
                os.write(buf, 0, n);
            in.close();
            os.close();
            return dst.getAbsolutePath();
        } catch (Exception e) {
            return null;
        }
    }

    static String nameOf(Context ctx, Uri uri) {
        try {
            Cursor c = ctx.getContentResolver().query(uri, null, null, null, null);
            if (c != null) {
                try {
                    int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (idx >= 0 && c.moveToFirst())
                        return c.getString(idx);
                } finally {
                    c.close();
                }
            }
        } catch (Exception e) {
        }
        return uri.getLastPathSegment();
    }

    static void writeResult(Context ctx, ArrayList<String> paths) {
        try {
            File f = new File(ctx.getCacheDir(), RESULT_FILE);
            BufferedWriter w = new BufferedWriter(new FileWriter(f));
            for (String p : paths) {
                w.write(p);
                w.newLine();
            }
            w.close();
        } catch (Exception e) {
        }
    }
}
