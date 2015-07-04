package com.fs.hotdog;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.IBinder;
import android.util.Log;

import org.apache.http.conn.util.InetAddressUtils;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.net.SocketAddress;
import java.util.Collections;
import java.util.List;

public class HotDogSer extends Service implements Runnable{
    private String TAG = "HotDog";
    private Socket mSocket = null;
    private boolean thread_disable = false;
    private String ser_ip = "125.95.190.162";
    private int ser_port = 49998;
    private String router_ip = "192.168.2.1";
    private int router_port = 50000;
    private int mtimeout = 500;
    private SocketAddress mAddress = new InetSocketAddress(ser_ip, ser_port);
    private SocketAddress rAddress = new InetSocketAddress(router_ip, router_port);
    private String send_buf = "hotdog";
    private String rcv_buf = null;
    private String rcv_key = null;
    private boolean auth_status = false;
    
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
    
    @Override
    public void onCreate() {
        super.onCreate();
        Log.e(TAG, "start create service!");
    }
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.e(TAG, "onStartCommand service!");
        Thread mThread = new Thread(this);
        mThread.start();
        return START_STICKY;
    }
    
    @Override
    public void onDestroy() {
        thread_disable = true;
        Log.e(TAG, "service destory!");
        super.onDestroy();
    }
    //获取mac地址 未联网是返回为0
    public String getlocalMacAddr()
    {
        WifiManager wifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE); 
        WifiInfo wifiInfo = (wifiManager == null ? null:wifiManager.getConnectionInfo());
        return wifiInfo.getMacAddress();
    }
    //获取ip地址，
    public static String getIPAddress(boolean useIPv4) {
        try {
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
                List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
            for (InetAddress addr : addrs) {
                if (!addr.isLoopbackAddress()) {
                String sAddr = addr.getHostAddress().toUpperCase();
                boolean isIPv4 = InetAddressUtils.isIPv4Address(sAddr);
                if (useIPv4) {
                   if (isIPv4)
                     return sAddr;
                   } else {
                    if (!isIPv4) {
                      int delim = sAddr.indexOf('%'); // drop ip6 port suffix
                      return delim < 0 ? sAddr : sAddr.substring(0, delim);
                        }
                    }
                  }
                }
            }
        } catch (Exception ex) {
        } 
        return "";
    }
    
    @Override
    public void run() {
        BufferedOutputStream bos = null;
        BufferedInputStream bis = null;
        byte[] byte_buf = new byte[1024];
        while(!thread_disable){
            if(!auth_status){//未认证，去认证
                try {
                    mSocket = new Socket();
                    mSocket.connect(mAddress, mtimeout);
                    bos = new BufferedOutputStream(mSocket.getOutputStream());
                    bis = new BufferedInputStream(mSocket.getInputStream());
                } catch (IOException e) {
                    e.printStackTrace();
                    continue;
                }
                if(mSocket.isConnected()){
                    try {
                        send_buf = new String("hotdog");
                        send_buf = send_buf + "#"+getIPAddress(true)+"@"+getlocalMacAddr()+"#";
                        Log.e(TAG, send_buf);
                        bos.write(send_buf.getBytes());
                        bos.flush();
                        bis.read(byte_buf);
                        rcv_buf = new String(byte_buf).trim();
                        Log.e(TAG, "rcv_buf = "+rcv_buf);
                        if(rcv_buf.equals(GlobalValues.ALLOW)){
                            Log.e(TAG, "auth succes!");
                            auth_status = true;
                            rcv_key = new String(rcv_buf);
                        }else if (rcv_buf.equals(GlobalValues.DENY)) {
                            Log.e(TAG, "auth failed!");
                            auth_status = false;
                        }
                    } catch (IOException e) {
                        e.printStackTrace();
                    }finally{
                        Log.e(TAG, "close everything!");
                        try {
                            mSocket.close();
                            mSocket = null;
                            bos.close();
                            bis.close();
                        } catch (Exception e) {
                            mSocket = null;
                            e.printStackTrace();
                        }
                    }
                }
            }else{//已经授权，直接上网，与路由器保持心跳
                try {
                    mSocket = new Socket();
                    mSocket.connect(rAddress, mtimeout);
                    bos = new BufferedOutputStream(mSocket.getOutputStream());
                    bis = new BufferedInputStream(mSocket.getInputStream());
                } catch (IOException e) {
                    e.printStackTrace();
                    continue;
                }
                while(mSocket.isConnected() && !thread_disable){
                    send_buf = new String(rcv_key);
                    try {
                        bos.write(send_buf.getBytes());
                        Log.e(TAG, "send_buf = "+send_buf);
                        bos.flush();
                        bis.read(byte_buf);
                        rcv_buf = new String(byte_buf).trim();
                        Log.e(TAG, "rcv_buf = "+rcv_buf);
                        Thread.sleep(5000);
                    } catch (Exception e) {
                        auth_status = false;
                        e.printStackTrace();
                    } 
                }
                auth_status = false;
                try {
                    if(mSocket.isConnected()){
                        bos.close();
                        bis.close();
                        mSocket.close();
                    }
                } catch (Exception e) {
                }
            }
        }
    }
}
