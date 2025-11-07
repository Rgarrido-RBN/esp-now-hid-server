# ESP32-S3 Clutch Paddles - ESP-NOW Receiver

An ESP-IDF based project for ESP32-S3 that receives data via ESP-NOW and forwards it to a Windows PC via USB/Serial connection.

## Project Structure

```
clutch_paddles/
├── CMakeLists.txt              # Root CMake configuration
├── sdkconfig.defaults          # ESP32-S3 default configuration
├── main/
│   ├── CMakeLists.txt          # Main component CMake
│   ├── main.c                  # Application entry point
│   ├── espnow_handler.c        # ESP-NOW communication handler
│   ├── usb_comm.c              # USB/Serial communication handler
│   ├── data_processor.c        # Data processing and formatting
│   └── include/
│       ├── espnow_handler.h    # ESP-NOW public API
│       ├── usb_comm.h          # USB communication public API
│       └── data_processor.h    # Data processor public API
└── esp-idf/                    # ESP-IDF framework (external)
```

## Features

- **ESP-NOW Reception**: Receives data from other ESP devices wirelessly
- **USB Communication**: Forwards received data to Windows PC via USB CDC
- **Modular Design**: Clean separation of concerns with dedicated modules
- **JSON Formatting**: Data is formatted as JSON for easy parsing on PC
- **Statistics**: Tracks packets received and bytes processed
- **Status Monitoring**: Periodic status updates sent to PC

## Modules

### ESP-NOW Handler (`espnow_handler.c/h`)
- Initializes WiFi and ESP-NOW protocol
- Manages peer devices
- Handles incoming ESP-NOW data
- Provides callback mechanism for received data

### USB Communication (`usb_comm.c/h`)
- Manages USB CDC serial communication
- Sends data to Windows PC
- Receives commands from PC
- Printf-style formatting support

### Data Processor (`data_processor.c/h`)
- Processes received ESP-NOW data
- Formats data as JSON or binary
- Maintains statistics
- Forwards data to USB module

### Main Application (`main.c`)
- Initializes all modules
- Coordinates data flow
- Monitors system status
- Entry point of application

## Building the Project

1. Set up ESP-IDF environment:
```bash
cd esp-idf
source export.sh
cd ..
```

2. Configure the project (optional):
```bash
idf.py menuconfig
```

3. Build the project:
```bash
idf.py build
```

4. Flash to ESP32-S3:
```bash
idf.py -p /dev/ttyUSB0 flash
```

5. Monitor output:
```bash
idf.py -p /dev/ttyUSB0 monitor
```

Or combine flash and monitor:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration

The `sdkconfig.defaults` file contains ESP32-S3 specific settings:
- Target chip: ESP32-S3
- Flash size: 4MB
- CPU frequency: 240MHz
- USB CDC console enabled
- WiFi and ESP-NOW optimizations

## Usage

1. **Power on the ESP32-S3** - Connect via USB to Windows PC

2. **Device starts automatically** and displays its MAC address

3. **ESP-NOW data reception** - Device automatically receives and processes ESP-NOW packets

4. **Data forwarding** - Received data is formatted as JSON and sent to PC via USB serial

5. **Monitor data on PC** - Use any serial terminal (e.g., PuTTY, Tera Term, Arduino Serial Monitor)
   - Baud rate: 115200
   - Data format: 8N1

## JSON Data Format

Received ESP-NOW data is sent to PC in JSON format:

```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "timestamp": 12345,
  "data_len": 10,
  "data": "01 02 03 04 05 06 07 08 09 0A",
  "rssi": -45
}
```

## Adding ESP-NOW Peers

To add a peer device programmatically, use:

```c
uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
espnow_handler_add_peer(peer_mac);
```

## Customization

### Change WiFi Channel
Edit `espnow_handler.c`, function `espnow_handler_init()`:
```c
esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);  // Change to channel 6
```

### Change USB Pins (if needed)
Edit `usb_comm.c`:
```c
#define USB_UART_TX_PIN 43
#define USB_UART_RX_PIN 44
```

### Modify Data Format
Edit `data_processor.c`, function `data_processor_format_json()` or create custom formatting.

## Troubleshooting

**ESP-NOW not receiving data:**
- Check WiFi channel matches sender device
- Verify MAC addresses are correct
- Ensure both devices are powered and initialized

**USB not working:**
- Check USB cable supports data transfer
- Verify correct COM port in Windows Device Manager
- Try different USB port

**Build errors:**
- Ensure ESP-IDF is properly installed and sourced
- Check ESP-IDF version compatibility
- Run `idf.py fullclean` and rebuild

## License

This project is provided as-is for development purposes.

## Development Notes

- Follow clean code principles
- Keep functions focused on single responsibility
- Use header files for public APIs
- Document all public functions
- Check return values and handle errors properly
