# MAX30102 SPO2 and pulse monitor
MAX30102 is a low cost high sensitivity pulse oximeter and heart rate sensor suitable for a wearable personal health monitor hacking. This project will use MAX30102, esp-01, and 3.8v Li battery to build a simple monitor with total cost under $25. The SPO2 and heart rate data will be store in Esp-01 SPIFFS, processed and shown with google chart API when browse the webpage hosted on esp-01. The purpose is to build a 24/7 wearable monitor, but the early test shown the data is very unreliable when move the hand/finger, which is unavoidable at daytime. Maybe a better designed finger holder, or better algorithm will be the next steps.

hardwares:
![MAX30102 Pulse Oximeter Heart-Rate Module](https://user-images.githubusercontent.com/24417162/124317646-ba945e00-db34-11eb-9377-a05b089cb1d6.png)
