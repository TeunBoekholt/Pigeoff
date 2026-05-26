graph TD
    classDef heltec fill:#3498db,stroke:#2980b9,color:#fff;
    classDef esp fill:#2ecc71,stroke:#27ae60,color:#fff;
    classDef lidar fill:#f1c40f,stroke:#f39c12,color:#333;
    classDef ttl fill:#95a5a6,stroke:#7f8c8d,color:#fff;

    subgraph Heltec V3 LoRa
        direction TB
        H_G2[GPIO 2]:::heltec
        H_G3[GPIO 3]:::heltec
        H_5V[5V]:::heltec
        H_GND[GND]:::heltec
        H_G5[GPIO 5]:::heltec
        H_G4[GPIO 4]:::heltec
    end

    subgraph ESP32-CAM
        direction TB
        E_G13[GPIO 13]:::esp
        E_G14[GPIO 14]:::esp
        E_U0T[U0T]:::esp
        E_GND[GND]:::esp
        E_U0R[U0R]:::esp
    end

    subgraph TF-Mini LiDAR
        direction TB
        L_RED[Red Wire / Power]:::lidar
        L_BLK[Black Wire / GND]:::lidar
        L_WHT[White Wire / TX]:::lidar
        L_GRN[Green Wire / RX]:::lidar
    end

    subgraph USB-to-TTL
        direction TB
        U_RX[RX]:::ttl
        U_GND[GND]:::ttl
        U_TX[TX]:::ttl
    end

    %% Communication Link
    H_G2 <-->|Comm Link| E_G13
    H_G3 <-->|Comm Link| E_G14

    %% LiDAR Connections
    H_5V -->|Power| L_RED
    H_GND ---|Ground| L_BLK
    H_G5 -->|TX to RX| L_WHT
    H_G4 <--|RX to TX| L_GRN

    %% Debug/Flashing
    E_U0T -->|TX to RX| U_RX
    E_GND ---|Ground| U_GND
    E_U0R <--|RX to TX| U_TX
