// 自定义入口 Activity：只干一件事——把窗口铺到系统栏（状态栏 /
// 导航栏）底下并让系统栏变透明。不然手机上状态栏那块是系统填的
// 死白，QML 里垫什么底图都看不见。
package com.pony.coursetable;

import android.graphics.Color;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

public class MainActivity extends org.qtproject.qt.android.bindings.QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        Window w = getWindow();
        // 系统栏透明：底图（bgfill.jpg）从它们底下透出来
        w.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        w.setStatusBarColor(Color.TRANSPARENT);
        w.setNavigationBarColor(Color.TRANSPARENT);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // API 30+：内容延伸到系统栏底下（边到边）
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController c = w.getInsetsController();
            if (c != null)
                c.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else {
            // 老版本：LAYOUT_* 三件套让内容铺满，不清掉系统栏
            w.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    // 系统文件选择器（Picker）的结果在这儿接住，转给它处理
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult(requestCode, resultCode, data);
        Picker.handleResult(this, requestCode, resultCode, data);
    }
}
