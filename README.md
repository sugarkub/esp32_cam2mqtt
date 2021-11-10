# esp32_cam2mqtt

![usecase_of_cam2mqtt](_photo/cam2mqtt.png)

원본 프로젝트는 suapapa님의 [esp32_cam2mqtt](https://github.com/suapapa/esp32_cam2mqtt)입니다.

* HomeAssistant의 [MQTT Camera](https://www.home-assistant.io/integrations/camera.mqtt/)를 이용하여 결과를 확인할 수 있습니다.
* [esp-idf](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/#)를 사용하여 빌드합니다.
* 하루에 한장의 사진을 찍도록 되어 있습니다.
* 날짜와 시간이 사진에 함께 출력됩니다. [sample](_photo/sample.png).

## 사전준비

- [esp-idf](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/#)

- [ESP32 Camera Driver](https://github.com/espressif/esp32-camera):
```bash
$ cd $IDF_PATH/components
$ git clone https://github.com/espressif/esp32-camera
```

## 환경설정 및 빌드

소스코드를 다운로드합니다.

```bash
$ git clone https://github.com/sugarkub/esp32_cam2mqtt
```

ESP-IDF Console에서 wifi-ssid, wifi-pass, mqtt-uri, mqtt-topic 등을 설정하기 위해 menuconfig를 실행합니다.

```bash
$ idf.py menuconfig
```

다음 내용을 설정합니다.
- Wifi Configuration: SSID 및 패스워드 설정
- MQTT Configuration: MQTT 서버 주소 설정
- Component config > ESP32-specific > Support for external, SPI-connected RAM 체크
- Serial flasher config > Flash size > 4MB 선택 (하드웨어에 따라 다를 수 있습니다.)

아래 명령어로 빌드할 수 있습니다.

```bash
$ idf.py build
$ idf.py -p COM4 flash
$ idf.py -p COM4 monitor
```

## Trouble shooting

X.509 인증서 관련해서 다음과 같은 빌드 오류가 나는 경우가 있습니다.

```bash
FAILED: esp-idf/mbedtls/x509_crt_bundle
```

아래와 같이 해결할 수 있습니다.

```bash
$ idf.py menuconfig
```

- Component Config > mbedTLS > Certificate Bundle > Default certificate bundle options > Use only the most certificates from the default bundles
