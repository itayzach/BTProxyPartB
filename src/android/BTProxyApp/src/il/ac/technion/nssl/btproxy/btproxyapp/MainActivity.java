package il.ac.technion.nssl.btproxy.btproxyapp;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.InetAddress;
import java.net.MalformedURLException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URL;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;


// =============================================================================
// MainActivity class
// =============================================================================
public class MainActivity extends Activity {
	private Button btnRestart;
	private Button btnClearLog;
	final Context context = this;
	private TextView tvCmdLog;
	private Handler updateActivityHandler;
	// -------------------------------------------------------------------------
	// TCP variables
	// -------------------------------------------------------------------------
	private TextView tvServerIP;
	private ServerSocket TCPServerSocket;
	private Socket TCPClientSocket = null;
	private final int CLOUD_SERVER_PORT = 4020; //Define the server port

	private final String CLOUD_SERVER_URL = "btpcs.eastus.cloudapp.azure.com";
	private Thread TCPclientThread = null;
	
	
	// -------------------------------------------------------------------------
	// BT variables
	// -------------------------------------------------------------------------
	BluetoothAdapter adapter = null;
	private TextView tvFoundBT;
	//private static String address = "D0:C1:B1:4B:EB:23";
	private BluetoothSocket BTSocket = null;
	private OutputStreamWriter BTOutputStream;
	private BluetoothDevice remoteDevice;
	private List<BTDeviceEntry> BTDevicesList;
	private final BroadcastReceiver discoveryResult = new BroadcastReceiver() {
		@Override
		public void onReceive(Context context, Intent intent) {
			android.util.Log.e("TrackingFlow", "[BroadcastReceiver] Entered onReceive");
	        String action = intent.getAction();
	        // When discovery finds a device
	        if (BluetoothDevice.ACTION_FOUND.equals(action)) {
	            // Get the BluetoothDevice object from the Intent
	            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
	            // add the name and address to the map
	            BTDeviceEntry btEntry = new BTDeviceEntry(device.getName(), device.getAddress());
	            if (!(device.getName().equals("BTPROXY"))) {
	            	BTDevicesList.add(btEntry);
		            updateActivityHandler.post(new updateUIThread("BTP added to BTDevicesList : " + btEntry.getName() + "[" + btEntry.getAddr() + "]"));
		            android.util.Log.e("TrackingFlow", "[BroadcastReceiver] Added - name = " + btEntry.getName() + " addr = " + btEntry.getAddr());
		            
	            } else {
	            	android.util.Log.e("TrackingFlow", "[BroadcastReceiver] Found myself");
	            }
	        }
		}
	};
	
	private void connectToBTdevice() { 
		try {
			android.util.Log.e("TrackingFlow", "[connectToBTdevice] Found: " + remoteDevice.getName());
			UUID uuid = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
			BTSocket = remoteDevice.createRfcommSocketToServiceRecord(uuid);
			BTSocket.connect();
			runOnUiThread(new Runnable() {	
				@Override
				public void run() {
					tvFoundBT.setText("Connected to BT device:\n" + remoteDevice.getName() + " [" + remoteDevice.getAddress() + "]");
				}
			});
			updateActivityHandler.post(new changeColorUIThread(Color.BLUE, tvFoundBT));
		}
		catch (IOException e) {e.printStackTrace();}
	}

	
	// -------------------------------------------------------------------------
	// OnCreate
	// -------------------------------------------------------------------------
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);
		
		tvServerIP = (TextView) findViewById(R.id.textViewServerIP);
		tvFoundBT = (TextView) findViewById(R.id.textViewFoundBT);
		tvCmdLog = (TextView) findViewById(R.id.textViewCmdLog);
		btnRestart = (Button) findViewById(R.id.btnRestart);
		btnClearLog = (Button) findViewById(R.id.btnClearLog);
		BTDevicesList = new ArrayList<BTDeviceEntry>();
		updateActivityHandler = new Handler();
		
		final String initBTfoundText = (String) tvFoundBT.getText();
		final String initServerIPText = (String) tvServerIP.getText(); 

		// ClearLog button listener
		btnClearLog.setOnClickListener(new View.OnClickListener() {
			
			@Override
			public void onClick(View v) {
				android.util.Log.e("TrackingFlow", "[btnClearLog] Clearing log");
				tvCmdLog.setText("");
			}
		});
		
		
		// Restart button listener
		btnRestart.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) { 
				android.util.Log.e("TrackingFlow", "[btnRestart] Restarting TCP server...");
				tvFoundBT.setText(initBTfoundText);
				tvFoundBT.setTextColor(Color.BLACK);
				tvServerIP.setText(initServerIPText);
				tvServerIP.setTextColor(Color.BLACK);
				tvCmdLog.setText("");
				try{
					if(BTSocket != null){
						BTOutputStream.close();
						BTSocket.close();
					}
					if (TCPClientSocket != null) {
						TCPClientSocket.close();
					}
					if (TCPServerSocket != null) {
						TCPServerSocket.close();
					}
				} catch(Exception e) {
					android.util.Log.e("TrackingFlow", "[btnRestart] onClick exception when closing sockets: " + e.getMessage());
				}
				TCPclientThread = new Thread(new TCPClientThread());
				TCPclientThread.start();
				
				if(adapter != null && adapter.isDiscovering()) {
					adapter.cancelDiscovery();
				}
				BTDevicesList.clear();

			}
		});

		// Start TCP client
		TCPclientThread = new Thread(new TCPClientThread());
		TCPclientThread.start();
		
		
	}
	// -------------------------------------------------------------------------
	// OnDestory
	// -------------------------------------------------------------------------
	@Override
	protected void onDestroy() {
		super.onDestroy();
		try{
			if(BTSocket != null){
				BTOutputStream.close();
				BTSocket.close();
			}
			if (TCPClientSocket != null) {
				TCPClientSocket.close();
			}
			if (TCPServerSocket != null) {
				TCPServerSocket.close();
			}
			BTDevicesList.clear();
			if(adapter != null && adapter.isDiscovering()) {
				unregisterReceiver(discoveryResult);
				adapter.cancelDiscovery();
			}
		} catch(Exception e) {
			android.util.Log.e("TrackingFlow", "[onDestroy] onDestroy exception when closing sockets: " + e.getMessage());
		}
	}

	// -------------------------------------------------------------------------
	// onCreateOptionsMenu
	// -------------------------------------------------------------------------
	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		getMenuInflater().inflate(R.menu.main, menu);
		return true;
	}

	// -------------------------------------------------------------------------
	// onOptionsItemSelected
	// -------------------------------------------------------------------------	
	@Override
	public boolean onOptionsItemSelected(MenuItem item) {
		int id = item.getItemId();
		if (id == R.id.action_settings) {
			return true;
		}
		return super.onOptionsItemSelected(item);
	}
	
	// Start this thread class in order to act as a TCP *server*
	// In this case, the BTClient (DLL) will connect to *this* device
	class TCPServerThread implements Runnable {
		@Override
		public void run() {
			try {
				TCPServerSocket = new ServerSocket(CLOUD_SERVER_PORT);
				android.util.Log.e("TrackingFlow", "[TCPServerThread] Created TCP server");
			} catch (IOException e) {
				e.printStackTrace();
			}
			try {
				android.util.Log.e("TrackingFlow", "[TCPServerThread] Waiting for TCP connections");
				TCPClientSocket = TCPServerSocket.accept();
				android.util.Log.e("TrackingFlow", "[TCPServerThread] Connection accepted");
				TCPCommThread TCPcommThread = new TCPCommThread();
				new Thread(TCPcommThread).start();

			} catch (IOException e) {
				e.printStackTrace();
			}
			android.util.Log.e("TrackingFlow", "[TCPServerThread] TCPServerThread ended");
		}
	}
	
	class TCPClientThread implements Runnable {

		@Override
		public void run() {
			InetAddress serverAddr;
			try {
				// connect to the cloud server IP though the TCPClientSocket
				android.util.Log.e("TrackingFlow", "[TCPClientThread] before serverAddr");
				serverAddr = InetAddress.getByName(CLOUD_SERVER_URL);
				android.util.Log.e("TrackingFlow", "[TCPClientThread] Found IP: " + serverAddr.getHostAddress());
				//serverAddr = getCloudIpAddress();
				TCPClientSocket = new Socket(serverAddr, CLOUD_SERVER_PORT);
				OutputStreamWriter out = new OutputStreamWriter(TCPClientSocket.getOutputStream());
				// send the id (btproxy)
				out.write("btproxy");
				out.flush();
				android.util.Log.e("TrackingFlow", "[TCPClientThread] sent btproxy as id to TCP server");
				// change the TextView color to identify the valid connection
				updateActivityHandler.post(new updateTVUIThread("\n" + CLOUD_SERVER_URL, tvServerIP));
				updateActivityHandler.post(new updateTVUIThread("\nIP: " + serverAddr.getHostAddress() + ":" + CLOUD_SERVER_PORT, tvServerIP));
				updateActivityHandler.post(new changeColorUIThread(Color.BLUE, tvServerIP));
				// run the comm thread for the DLL<->BTP messages pipe
				TCPCommThread TCPcommThread = new TCPCommThread();
				new Thread(TCPcommThread).start();
			} catch (UnknownHostException e) {
				e.printStackTrace();
			} catch (IOException e) {
				e.printStackTrace();
			}
		}

	}
	
	class TCPCommThread implements Runnable {

		private BufferedReader TCPbufReader;
		private int BTdevicesListIndex = 0;
		public TCPCommThread() {
			try {
				this.TCPbufReader = new BufferedReader(new InputStreamReader(TCPClientSocket.getInputStream()));
			} catch (IOException e) {
				e.printStackTrace();
			}
		}

		@Override
		public void run() {
			BTDeviceEntry BTdeviceEntry = null;
			while (!Thread.currentThread().isInterrupted() && !TCPClientSocket.isClosed()) {
				try {
					// read TCP command
					String TCPreadLine = "";
					byte[] TCPbyteBuf = new byte[1024];
					ByteArrayOutputStream byteArrayOutputStream = 
			                  new ByteArrayOutputStream(1024);
					int TCPbytesRead = 0;
					InputStream TCPinputStream = TCPClientSocket.getInputStream();
					android.util.Log.e("TrackingFlow", "[TCPClientThread] before read. BTdevicesListIndex = " + BTdevicesListIndex + " size = " + BTDevicesList.size());
					//String TCPreadLine = TCPbufReader.readLine();
					TCPbytesRead = TCPinputStream.read(TCPbyteBuf);
					android.util.Log.e("TrackingFlow", "[TCPClientThread] read " + TCPbytesRead);
					if (TCPbytesRead < 0) {
						updateActivityHandler.post(new updateUIThread("HKW->BTP : Read 0 bytes"));
						break;
					}
					byteArrayOutputStream.write(TCPbyteBuf, 0, TCPbytesRead);
					TCPreadLine += byteArrayOutputStream.toString("UTF-8");
					//TCPreadLine = TCPreadLine.replaceAll("\\s",""); // remove white spaces
					android.util.Log.e("TrackingFlow", "[TCPClientThread] read : " + TCPreadLine);
					
					if (TCPreadLine.equals("msgFromHKW_queryDevice")) {
						android.util.Log.e("TrackingFlow", "[TCPClientThread] received : msgFromHKW_queryDevice");
						updateActivityHandler.post(new updateUIThread("HKW->BTP : queryDevice"));
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						if (BTdevicesListIndex == 0) {
							// Start BT discovery
							registerReceiver(discoveryResult, new IntentFilter(BluetoothDevice.ACTION_FOUND));
							adapter = BluetoothAdapter.getDefaultAdapter();
							if(adapter.isDiscovering()) {
								android.util.Log.e("TrackingFlow", "[TCPClientThread] cancaling discovery");
								adapter.cancelDiscovery();
							}
							adapter.startDiscovery();
							android.util.Log.e("TrackingFlow", "[TCPClientThread] started discovery");
							int discoveryTries = 0;
							while ((BTDevicesList.size() == 0) && discoveryTries < 5) {
								android.util.Log.e("TrackingFlow", "[TCPClientThread] sleeping for 500ms, tries = " + discoveryTries);
								Thread.sleep(500);
								discoveryTries++;
							}
							if (BTDevicesList.size() == 0) {
								android.util.Log.e("TrackingFlow", "[TCPClientThread] didn't find any BT devices");
								android.util.Log.e("TrackingFlow", "[TCPClientThread] sending back msgFromBTP_WSA_E_NO_MORE");
								TCPoutput.write("msgFromBTP_WSA_E_NO_MORE");
								TCPoutput.flush();
								updateActivityHandler.post(new updateUIThread("BTP->HKW : WSA_E_NO_MORE"));
								continue;
							}
						} else if (BTdevicesListIndex == BTDevicesList.size()) {
							// meaning there are no more devices found
							android.util.Log.e("TrackingFlow", "[TCPClientThread] sending back msgFromBTP_WSA_E_NO_MORE");
							TCPoutput.write("msgFromBTP_WSA_E_NO_MORE");
							TCPoutput.flush();
							updateActivityHandler.post(new updateUIThread("BTP->HKW : WSA_E_NO_MORE"));
							continue;
						}

						BTdeviceEntry = BTDevicesList.get(BTdevicesListIndex);
						android.util.Log.e("TrackingFlow", "[TCPClientThread] sending back this : " + BTdeviceEntry.getName());
						android.util.Log.e("TrackingFlow", "[TCPClientThread] matching index : " + BTdevicesListIndex);
						TCPoutput.write(BTdeviceEntry.getName());
						TCPoutput.flush();
						BTdevicesListIndex++;
						updateActivityHandler.post(new updateUIThread("BTP->HKW : " + BTdeviceEntry.getName()));
					}
					else if (TCPreadLine.equals("msgFromHKW_connect")) {
						android.util.Log.e("TrackingFlow", "[TCPClientThread] received : msgFromHKW_connect");
						android.util.Log.e("TrackingFlow", "[TCPClientThread] MAC : " + BTdeviceEntry.getAddr());
						updateActivityHandler.post(new updateUIThread("HKW->BTP : connect"));
						remoteDevice = adapter.getRemoteDevice(BTdeviceEntry.getAddr());
						android.util.Log.e("TrackingFlow", "[TCPClientThread] after remoteDevice assign");
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						android.util.Log.e("TrackingFlow", "[TCPClientThread] connecting to " + BTdeviceEntry.getAddr() + "...");
						if (BTSocket == null) {
							connectToBTdevice();
							android.util.Log.e("TrackingFlow", "[TCPClientThread] Connected to BT device");
						} else if (!BTSocket.isConnected()) {
							connectToBTdevice();
							android.util.Log.e("TrackingFlow", "[TCPClientThread] Connected to BT device");
						}
						TCPoutput.write("msgFromBTP_connectedTo_" + BTdeviceEntry.getAddr());
						TCPoutput.flush();
						updateActivityHandler.post(new updateUIThread("BTP->HKW : connected to " + BTdeviceEntry.getAddr()));
						updateActivityHandler.post(new changeColorUIThread(Color.BLUE, tvServerIP));
					}
					else if (TCPreadLine.equals("msgFromHKW_send")) {
						updateActivityHandler.post(new updateUIThread("HKW->BTP : send"));
						// read actual message from DLL
						android.util.Log.e("TrackingFlow", "[TCPClientThread] received : msgFromHKW_send");
						BTOutputStream = new OutputStreamWriter(BTSocket.getOutputStream());
						android.util.Log.e("TrackingFlow", "[TCPClientThread] waiting for TCP message...");
						TCPreadLine = TCPbufReader.readLine();
						updateActivityHandler.post(new updateUIThread("HKW->BTP : " + TCPreadLine));
						android.util.Log.e("TrackingFlow", "[TCPClientThread] sending to BT device the following : " + TCPreadLine);
						BTOutputStream.write(TCPreadLine + System.getProperty("line.separator"));
						BTOutputStream.flush();
						updateActivityHandler.post(new updateUIThread("BTP->BTD : " + TCPreadLine));
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						TCPoutput.write("msgFromBTP_sentToBTdevice");
						TCPoutput.flush();
						updateActivityHandler.post(new updateUIThread("BTP->HKW : sentToBTdevice"));
						android.util.Log.e("TrackingFlow", "[TCPClientThread] finished sending");
					}
					else if (TCPreadLine.equals("msgFromHKW_closeSockets")) {
						android.util.Log.e("TrackingFlow", "[TCPClientThread] received : msgFromHKW_closeSockets");
						if (BTSocket != null) {
							updateActivityHandler.post(new changeColorUIThread(Color.BLACK, tvFoundBT));
							BTOutputStream.close();
							BTSocket.close();
						}
						updateActivityHandler.post(new updateUIThread("HKW->BTP : closeSockets"));
						BTDevicesList.clear();
						BTdevicesListIndex = 0;
						if(adapter != null && adapter.isDiscovering()) {
							adapter.cancelDiscovery();
						}
						updateActivityHandler.post(new updateUIThread("BTP cleared devices list"));
						
					}
					android.util.Log.e("TrackingFlow", "[TCPClientThread] end of iteration");

				} catch (IOException e) {
					e.printStackTrace();
				} catch (InterruptedException e) {
					e.printStackTrace();
				}
			}
			android.util.Log.e("TrackingFlow", "[TCPClientThread] TCPCommThread was ended");
			updateActivityHandler.post(new changeColorUIThread(Color.BLACK, tvServerIP));
			
		}
	}
	
	class updateTVUIThread implements Runnable {
		private String msg;
		private TextView tv;
		private final StringBuilder sb = new StringBuilder();
		
		public updateTVUIThread(String result, TextView tv) {
			this.msg = result;
			this.tv = tv;
			sb.append(msg);
		}

		@Override
		public void run() {
			// add to scrollview the commands log:
			tv.setText(tv.getText().toString() + msg);
		}
	}
	
	class updateUIThread implements Runnable {
		private String msg;
		private final StringBuilder sb = new StringBuilder();
		
		public updateUIThread(String result) {
			this.msg = result;
			sb.append(msg);
		}

		@Override
		public void run() {
			// add to scrollview the commands log:
			tvCmdLog.setText(tvCmdLog.getText().toString() + msg + "\n");
		}
	}
	
	class changeColorUIThread implements Runnable {
		private int color;
		private TextView tv;
		
		public changeColorUIThread(int color, TextView tv) {
			this.color = color;
			this.tv = tv;
		}

		@Override
		public void run() {
			tv.setTextColor(color);
		}
	}
	
	class BTDeviceEntry {
		
		private String name;
		private String addr;
		
		public BTDeviceEntry(String name, String addr) {
			this.name = new String(name);
			this.addr = new String(addr);
		}
		
		public String getName() {
			return name;
		}
		
		public String getAddr() {
			return addr;
		}
	}
}

