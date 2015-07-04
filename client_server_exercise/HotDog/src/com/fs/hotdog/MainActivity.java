package com.fs.hotdog;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

@SuppressLint("HandlerLeak")
public class MainActivity extends Activity{
    Intent mIntent = new Intent("com.fs.hotdog.HotDogSer");
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        this.startService(mIntent);
    }
    
    @Override
    protected void onDestroy() {
        // TODO Auto-generated method stub
        super.onDestroy();
    }
}
