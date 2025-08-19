package com.example.libcorotest;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.widget.ScrollView;
import android.widget.TextView;

public class MainActivity extends Activity {
    static { System.loadLibrary("coroTest"); }

    private TextView textView;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView sv = new ScrollView(this);
        textView = new TextView(this);
        int pad = (int) (8 * getResources().getDisplayMetrics().density);
        textView.setPadding(pad, pad, pad, pad);
        sv.addView(textView, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(sv);

        // Run tests on a worker thread; when finished, set result for CI and close.
        new Thread(() -> {
            int rc = runTests(getFilesDir().getAbsolutePath());
            ui.post(() -> {
                appendLine("Finished with code=" + rc);
                setResult(rc == 0 ? RESULT_OK : 1234);
                finish();
            });
        }, "tests").start();
    }

    // Called from native
    public void appendLine(String s) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            textView.append(s + "\n");
            // Auto-scroll to bottom
            ((ScrollView) textView.getParent()).post(() -> ((ScrollView) textView.getParent()).fullScroll(ScrollView.FOCUS_DOWN));
        } else {
            ui.post(() -> appendLine(s));
        }
    }

    public native int runTests(String filesDir);
}
