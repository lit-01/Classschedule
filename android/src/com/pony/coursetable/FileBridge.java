package com.pony.coursetable;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Base64;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/**
 * 系统文件选择器（SAF）返回的是 content:// URI，
 * QFileInfo 拿不到文件名、QFile 也读不了，得由 Java 层用 ContentResolver 来。
 */
public class FileBridge {

    /** 附件的显示名；拿不到就返回空串 */
    public static String displayName(Context ctx, String uriStr) {
        try {
            Uri uri = Uri.parse(uriStr);
            ContentResolver cr = ctx.getContentResolver();
            Cursor c = cr.query(uri, null, null, null, null);
            if (c != null) {
                try {
                    int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (idx >= 0 && c.moveToFirst()) {
                        String n = c.getString(idx);
                        if (n != null && !n.isEmpty())
                            return n;
                    }
                } finally {
                    c.close();
                }
            }
            String last = uri.getLastPathSegment();
            return last == null ? "" : last;
        } catch (Exception e) {
            return "";
        }
    }

    /** 把 content:// 的内容整个读出来，Base64 编码返回；空串表示失败 */
    // 失败时返回 "ERR:异常"，C++ 那边直接显示给用户，方便定位
    public static String readBase64(Context ctx, String uriStr) {
        try {
            Uri uri = Uri.parse(uriStr);
            InputStream in = ctx.getContentResolver().openInputStream(uri);
            if (in == null)
                return "ERR:openInputStream 返回空";
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0)
                out.write(buf, 0, n);
            in.close();
            return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP);
        } catch (Exception e) {
            return "ERR:" + (e.getMessage() == null ? e.toString() : e.getMessage());
        }
    }
}
