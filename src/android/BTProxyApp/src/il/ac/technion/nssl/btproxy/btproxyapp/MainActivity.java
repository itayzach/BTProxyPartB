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
import java.net.ServerSocket;
import java.net.Socket;
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


// TODO
// 1. Add clear log button
// 2. check the discovery issue
// =============================================================================
// MainActivity class
// =============================================================================
public class MainActivity extends Activity {
	private Button btnRestart;
	final Context context = this;
	private TextView tvCmdLog;
	private Handler updateConversationHandler;
	private Handler changeColorUIThread;
	// -------------------------------------------------------------------------
	// TCP variables
	// -------------------------------------------------------------------------
	private TextView tvServerIP;
	private ServerSocket TCPServerSocket;
	private Socket TCPClientSocket = null;
	private final int CLOUD_SERVER_PORT = 4020; //Define the server port
	private final String CLOUD_SERVER_IP = "132.68.60.117";
	//private Thread TCPserverThread = null;
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
	private boolean startedDiscovery = false;
	private final BroadcastReceiver discoveryResult = new BroadcastReceiver() {
		@Override
		public void onReceive(Context context, Intent intent) {
			android.util.Log.e("TrackingFlow", "Entered onReceive");
	        String action = intent.getAction();
	        // When discovery finds a device
	        if (BluetoothDevice.ACTION_FOUND.equals(action)) {
	            // Get the BluetoothDevice object from the Intent
	            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
	            // add the name and address to the map
	            BTDeviceEntry btEntry = new BTDeviceEntry(device.getName(), device.getAddress());
	            BTDevicesList.add(btEntry);
	            updateConversationHandler.post(new updateUIThread("BTP added to BTDevicesList : " + btEntry.getName() + "[" + btEntry.getAddr() + "]"));
	            android.util.Log.e("TrackingFlow", "Added - name = " + btEntry.getName() + " addr = " + btEntry.getAddr());
	            
	        }
	        startedDiscovery = true;
		}
	};
	
	private void connectToBTdevice() { 
		try {
			android.util.Log.e("TrackingFlow", "Found: " + remoteDevice.getName());
			UUID uuid = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
			BTSocket = remoteDevice.createRfcommSocketToServiceRecord(uuid);
			runOnUiThread(new Runnable() {	
				@Override
				public void run() {
					tvFoundBT.append("\n" + remoteDevice.getName() + " [" + remoteDevice.getAddress() + "]");
				}
			});
			BTSocket.connect();
			changeColorUIThread.post(new changeColorUIThread(Color.BLUE, tvFoundBT));
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
		BTDevicesList = new ArrayList<BTDeviceEntry>();
		updateConversationHandler = new Handler();
		changeColorUIThread = new Handler();
		final String initBTfoundText = (String) tvFoundBT.getText(); 
		
		btnRestart.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) { 
				android.util.Log.e("TrackingFlow", "Restarting TCP server...");
				tvFoundBT.setText(initBTfoundText);
				tvFoundBT.setTextColor(Color.BLACK);
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
					android.util.Log.e("TrackingFlow", "onClick exception when closing sockets: " + e.getMessage());
				}
				//TCPserverThread = new Thread(new TCPServerThread());
				//TCPserverThread.start();
				TCPclientThread = new Thread(new TCPClientThread());
				TCPclientThread.start();
				
				// BT discovery
//				unregisterReceiver(discoveryResult);
//				registerReceiver(discoveryResult, new IntentFilter(BluetoothDevice.ACTION_FOUND));
//				
//				adapter = BluetoothAdapter.getDefaultAdapter();
				if(adapter != null && adapter.isDiscovering()) {
					adapter.cancelDiscovery();
				}
				BTDevicesList.clear();
				startedDiscovery = false;
				//adapter.startDiscovery();

			}
		});

		// Find IP
		//getDeviceIpAddress();
		getCloudIpAddress();
		
		// Wait for TCP connection from client
		// TCPserverThread = new Thread(new TCPServerThread());
		// android.util.Log.e("TrackingFlow", "Before start");
		// TCPserverThread.start();
		
		// Connect to cloud server
		TCPclientThread = new Thread(new TCPClientThread());
		TCPclientThread.start();
		
		// Start BT discovery
		registerReceiver(discoveryResult, new IntentFilter(BluetoothDevice.ACTION_FOUND));
		adapter = BluetoothAdapter.getDefaultAdapter();
//		if(adapter != null && adapter.isDiscovering()) {
//			adapter.cancelDiscovery();
//		}
//		adapter.startDiscovery();

		
	}
	// -------------------------------------------------------------------------
	// OnDestory
	// -------------------------------------------------------------------------
	@Override
	protected void onDestroy() {
		super.onDestroy();
		/*try {
			unregisterReceiver(discoveryResult);
		} catch(Exception e) {
			android.util.Log.e("TrackingFlow", "onDestroy exception in unregisterReciever : " + e.getMessage());
		}*/
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
			android.util.Log.e("TrackingFlow", "onDestroy exception when closing sockets: " + e.getMessage());
		}
	}

	// -------------------------------------------------------------------------
	// getCloudIpAddress
	// -------------------------------------------------------------------------
	private void getCloudIpAddress() {
		//inetAddr = InetAddress.getByName(<Microsoft Azure address>);
		//tvServerIP.append(" " + inetAddr.toString());
		tvServerIP.append(" " + CLOUD_SERVER_IP + ":" + CLOUD_SERVER_PORT);
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
				android.util.Log.e("TrackingFlow", "Created TCP server");
			} catch (IOException e) {
				e.printStackTrace();
			}
			try {
				android.util.Log.e("TrackingFlow", "Waiting for TCP connections");
				TCPClientSocket = TCPServerSocket.accept();
				android.util.Log.e("TrackingFlow", "Connection accepted");
				TCPCommThread TCPcommThread = new TCPCommThread();
				new Thread(TCPcommThread).start();

			} catch (IOException e) {
				e.printStackTrace();
			}
			android.util.Log.e("TrackingFlow", "TCPServerThread ended");
		}
	}
	
	class TCPClientThread implements Runnable {

		@Override
		public void run() {
			InetAddress serverAddr;
			try {
				// connect to the cloud server IP though the TCPClientSocket
				serverAddr = InetAddress.getByName(CLOUD_SERVER_IP);
				TCPClientSocket = new Socket(serverAddr, CLOUD_SERVER_PORT);
				OutputStreamWriter out = new OutputStreamWriter(TCPClientSocket.getOutputStream());
				// send the id (btproxy)
				out.write("btproxy");
				out.flush();
				android.util.Log.e("TrackingFlow", "sent btproxy as id to TCP server");
				// change the TextView color to identify the valid conenction
				changeColorUIThread.post(new changeColorUIThread(Color.BLUE, tvServerIP));
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
					android.util.Log.e("TrackingFlow", "before read. BTdevicesListIndex = " + BTdevicesListIndex + " size = " + BTDevicesList.size());
					//String TCPreadLine = TCPbufReader.readLine();
					TCPbytesRead = TCPinputStream.read(TCPbyteBuf);
					android.util.Log.e("TrackingFlow", "read " + TCPbytesRead);
					if (TCPbytesRead < 0) {
						updateConversationHandler.post(new updateUIThread("DLL->BTP : Read 0 bytes"));
						break;
					}
					byteArrayOutputStream.write(TCPbyteBuf, 0, TCPbytesRead);
					TCPreadLine += byteArrayOutputStream.toString("UTF-8");
					//TCPreadLine = TCPreadLine.replaceAll("\\s",""); // remove white spaces
					android.util.Log.e("TrackingFlow", "read : " + TCPreadLine);
					
					if (TCPreadLine.equals("msgFromDLL_queryDevice")) {
						android.util.Log.e("TrackingFlow", "received : msgFromDLL_queryDevice");
						updateConversationHandler.post(new updateUIThread("DLL->BTP : queryDevice"));
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						if (BTdevicesListIndex == 0) {
							// Start BT discovery
							if(adapter.isDiscovering()) {
								android.util.Log.e("TrackingFlow", "cancaling discovery");
								adapter.cancelDiscovery();
							}
							adapter.startDiscovery();
							android.util.Log.e("TrackingFlow", "started discovery");
							//while(!startedDiscovery);
							//android.util.Log.e("TrackingFlow", "finished discovery");
						} else if (BTdevicesListIndex == BTDevicesList.size()) {
							// meaning there are no more devices found
							android.util.Log.e("TrackingFlow", "sending back msgFromBTProxy_WSA_E_NO_MORE");
							TCPoutput.write("msgFromBTProxy_WSA_E_NO_MORE");
							TCPoutput.flush();
							updateConversationHandler.post(new updateUIThread("BTP->DLL : WSA_E_NO_MORE"));
							continue;
						}
						while(BTDevicesList.size() == 0);
						android.util.Log.e("TrackingFlow", "A device was added");
						BTdeviceEntry = BTDevicesList.get(BTdevicesListIndex);
						android.util.Log.e("TrackingFlow", "sending back this : " + BTdeviceEntry.getName());
						android.util.Log.e("TrackingFlow", "matching index : " + BTdevicesListIndex);
						TCPoutput.write(BTdeviceEntry.getName());
						TCPoutput.flush();
						BTdevicesListIndex++;
						updateConversationHandler.post(new updateUIThread("BTP->DLL : " + BTdeviceEntry.getName()));
					}
					else if (TCPreadLine.equals("msgFromDLL_connect")) {
						android.util.Log.e("TrackingFlow", "received : msgFromDLL_connect");
						android.util.Log.e("TrackingFlow", "MAC : " + BTdeviceEntry.getAddr());
						updateConversationHandler.post(new updateUIThread("DLL->BTP : connect"));
						remoteDevice = adapter.getRemoteDevice(BTdeviceEntry.getAddr());
						android.util.Log.e("TrackingFlow", "after remoteDevice assign");
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						android.util.Log.e("TrackingFlow", "connecting to " + BTdeviceEntry.getAddr() + "...");
						if (BTSocket == null) {
							connectToBTdevice();
							android.util.Log.e("TrackingFlow", "Connected to BT device");
						} else if (!BTSocket.isConnected()) {
							connectToBTdevice();
							android.util.Log.e("TrackingFlow", "Connected to BT device");
						}
						TCPoutput.write("msgFromBTProxy_connectedTo_" + BTdeviceEntry.getAddr());
						TCPoutput.flush();
						updateConversationHandler.post(new updateUIThread("BTP->DLL : connected to " + BTdeviceEntry.getAddr()));
						changeColorUIThread.post(new changeColorUIThread(Color.BLUE, tvServerIP));
					}
					else if (TCPreadLine.equals("msgFromDLL_send")) {
						updateConversationHandler.post(new updateUIThread("DLL->BTP : send"));
						// read actual message from DLL
						android.util.Log.e("TrackingFlow", "received : msgFromDLL_send");
						BTOutputStream = new OutputStreamWriter(BTSocket.getOutputStream());
						android.util.Log.e("TrackingFlow", "waiting for TCP message...");
						TCPreadLine = TCPbufReader.readLine();
						updateConversationHandler.post(new updateUIThread("DLL->BTP : " + TCPreadLine));
						android.util.Log.e("TrackingFlow", "sending to BT device the following : " + TCPreadLine);
						BTOutputStream.write(TCPreadLine + System.getProperty("line.separator"));
						BTOutputStream.flush();
						updateConversationHandler.post(new updateUIThread("BTP->BTD : " + TCPreadLine));
						PrintWriter TCPoutput = new PrintWriter(new BufferedWriter(
								new OutputStreamWriter(TCPClientSocket.getOutputStream())),
								true);
						TCPoutput.write("msgFromBTProxy_sentToBTdevice");
						TCPoutput.flush();
						updateConversationHandler.post(new updateUIThread("BTP->DLL : sentToBTdevice"));
						android.util.Log.e("TrackingFlow", "finished sending");
					}
					else if (TCPreadLine.equals("msgFromDLL_closeSockets")) {
						android.util.Log.e("TrackingFlow", "received : msgFromDLL_closeSockets");
						if (BTSocket != null) {
							changeColorUIThread.post(new changeColorUIThread(Color.BLACK, tvFoundBT));
							BTOutputStream.close();
							BTSocket.close();
						}
						updateConversationHandler.post(new updateUIThread("DLL->BTP : closeSockets"));
						BTDevicesList.clear();
						BTdevicesListIndex = 0;
						if(adapter != null && adapter.isDiscovering()) {
							adapter.cancelDiscovery();
						}
						updateConversationHandler.post(new updateUIThread("BTP cleared devices list"));
						
					}
					android.util.Log.e("TrackingFlow", "end of iteration");

				} catch (IOException e) {
					e.printStackTrace();
				}
			}
			android.util.Log.e("TrackingFlow", "TCPCommThread was ended");
			
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

