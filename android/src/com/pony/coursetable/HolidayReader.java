package com.pony.coursetable;

import android.content.Context;
import android.database.Cursor;
import android.provider.CalendarContract;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * 去翻手机自带的日历，把「法定节假日」和「调休补班日」捞出来。
 *
 * 各家 ROM（MIUI / HarmonyOS / ColorOS / Google）都会在系统日历里塞一份
 * 节假日日历，名字通常是「中国法定节假日」「节假日」「Holidays in China」。
 * 这里不去猜 calendar_type 的常量值（AOSP 各版本不一样），直接按日历名字里
 * 有没有「假」或者「holiday」来认，兼容性好一点。
 *
 * 调休那些天在节假日日历里是一条标题带「班 / 调休 / 补班」的全天事件，
 * 靠标题区分。
 *
 * 返回格式（给 Qt 的 JNI 调用）：
 *     "2026-01-01,2026-01-02|2026-01-04"
 *     节假日列表            |  调休补班列表
 */
public class HolidayReader {

    public static String read(Context context, long beginMillis, long endMillis) {
        if (context == null)
            return "|";

        HashSet<String> holidays = new HashSet<String>();
        HashSet<String> workdays = new HashSet<String>();

        // ---------- 1. 找出所有节假日日历 ----------
        List<Long> holidayCalendars = new ArrayList<Long>();
        Cursor calendars = null;
        try {
            calendars = context.getContentResolver().query(
                    CalendarContract.Calendars.CONTENT_URI,
                    new String[]{
                            CalendarContract.Calendars._ID,
                            CalendarContract.Calendars.CALENDAR_DISPLAY_NAME,
                            CalendarContract.Calendars.ACCOUNT_NAME
                    },
                    null, null, null);

            if (calendars != null) {
                while (calendars.moveToNext()) {
                    long id = calendars.getLong(0);
                    String name = calendars.isNull(1) ? "" : calendars.getString(1);
                    String account = calendars.isNull(2) ? "" : calendars.getString(2);
                    String blob = (name + " " + account).toLowerCase(Locale.ROOT);

                    if (blob.contains("假") || blob.contains("holiday"))
                        holidayCalendars.add(Long.valueOf(id));
                }
            }
        } catch (Exception ignored) {
            // 没权限 / ROM 不支持，交给 Qt 那边走兜底表
        } finally {
            if (calendars != null)
                calendars.close();
        }

        if (holidayCalendars.isEmpty())
            return "|";

        // ---------- 2. 查这些日历里的全天事件 ----------
        StringBuilder selection = new StringBuilder();
        selection.append(CalendarContract.Instances.CALENDAR_ID).append(" IN (");
        for (int i = 0; i < holidayCalendars.size(); i++) {
            if (i > 0)
                selection.append(',');
            selection.append(holidayCalendars.get(i).longValue());
        }
        selection.append(") AND ")
                .append(CalendarContract.Instances.BEGIN).append(" >= ").append(beginMillis)
                .append(" AND ")
                .append(CalendarContract.Instances.BEGIN).append(" <= ").append(endMillis)
                .append(" AND ")
                .append(CalendarContract.Instances.ALL_DAY).append(" = 1");

        // 全天事件的时间戳是「当天 UTC 零点」，所以要按 UTC 换算回日期，
        // 用本地时区会整体偏一天。
        Calendar utc = Calendar.getInstance(TimeZone.getTimeZone("UTC"));

        Cursor instances = null;
        try {
            instances = context.getContentResolver().query(
                    CalendarContract.Instances.CONTENT_URI,
                    new String[]{
                            CalendarContract.Instances.BEGIN,
                            CalendarContract.Instances.TITLE
                    },
                    selection.toString(), null, null);

            if (instances != null) {
                while (instances.moveToNext()) {
                    long begin = instances.getLong(0);
                    String title = instances.isNull(1) ? "" : instances.getString(1);

                    utc.setTimeInMillis(begin);
                    String iso = String.format(Locale.US, "%04d-%02d-%02d",
                            utc.get(Calendar.YEAR),
                            utc.get(Calendar.MONTH) + 1,
                            utc.get(Calendar.DAY_OF_MONTH));

                    if (isMakeupWorkday(title))
                        workdays.add(iso);
                    else
                        holidays.add(iso);
                }
            }
        } catch (Exception ignored) {
            // 同上，读不到就走兜底
        } finally {
            if (instances != null)
                instances.close();
        }

        // 同一天既算假日又算调休的话，以调休为准
        holidays.removeAll(workdays);

        return join(holidays) + "|" + join(workdays);
    }

    /** 标题里带「班 / 调休 / 补班 / workday」的，是调休要上课那天 */
    private static boolean isMakeupWorkday(String title) {
        if (title == null || title.isEmpty())
            return false;
        String t = title.toLowerCase(Locale.ROOT);
        return t.contains("班") || t.contains("调休") || t.contains("workday");
    }

    private static String join(HashSet<String> set) {
        StringBuilder sb = new StringBuilder();
        for (String s : set) {
            if (sb.length() > 0)
                sb.append(',');
            sb.append(s);
        }
        return sb.toString();
    }
}
