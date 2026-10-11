# Motosiklet Yatış Açısı Göstergesi

**Racing tipi motosiklet için bataryalı yatış açısı göstergesi: ESP32 + MPU-6050, 128×64 OLED yay gösterge.**

🇬🇧 [English README](README.md)

<!-- TODO: çalışan ekranın fotoğrafı veya kısa GIF'i: docs/images/demo.gif -->

## Özellikler

- Jiroskop + ivmeölçer tamamlayıcı filtresiyle **anlık yatış açısı**, ~20 Hz ekran
- Kapatınca silinmeyen **sol/sağ maksimum yatış** (ESP32 NVS)
- Max noktalı, ortada büyük rakamlı **animasyonlu yarım yay gösterge**, açılış animasyonu
- Köşede **ortam sıcaklığı** (DHT11)
- **Tek buton:** kısa basış montaj sıfırını kaydeder (cihaz 0,4 sn sabitse, değilse "HAREKET VAR"), 2 sn uzun basış max değerlerini siler
- Her açılışta **jiro bias kalibrasyonu**
- 18650 Li-ion pil, koruma devresi ve 5 V boost

## Nasıl çalışır

Jiroskoptaki yatış hızı her döngüde entegre edilir. İvmeölçer açısı (`atan2(ay, az)`) tahmini yavaşça düzeltir (kazanç 0,01), ama **yalnızca toplam ivme ~1 g iken** (0,9–1,1 g). Virajda yanal yük olduğu için ivmeölçer tek başına yanlış açı verir; bu sınır sayesinde virajda jiro hâkim kalır, düzlükte kayma temizlenir. Max değerleri son artıştan 2 sn sonra flash'a yazılır.

## Bağlantılar

| Sinyal | ESP32 pini |
|---|---|
| I²C SDA (OLED, MPU-6050) | GPIO21 |
| I²C SCL (OLED, MPU-6050) | GPIO22 |
| DHT11 veri | GPIO18 |
| Buton (diğer bacak GND) | GPIO4 (`INPUT_PULLUP`) |
| Besleme (1N5819 üzerinden) | VIN |

Parça listesi, güç, delik haritası ve kasa: [docs/hardware.md](docs/hardware.md)

## Derleme ve yükleme

1. Arduino IDE ve Espressif ESP32 kart paketini kurun.
2. Kütüphaneler: Adafruit GFX, Adafruit SSD1306, DHT sensor library.
3. `firmware/yatis_gostergesi/yatis_gostergesi.ino` dosyasını açın, **ESP32 Dev Module** seçip yükleyin.
4. Açılışta birkaç saniye cihazı oynatmayın (kalibrasyon). Dik monte edip bir kez kısa basın.

Sağ/sol ters çıkarsa `#define SIGN -1` satırını `1` yapın.

## Yol haritası

- [x] Yazılım: filtre, NVS max, yay gösterge, buton
- [x] Tek pille güç testi
- [ ] Gerçek akım ölçümü ve pil süresi
- [ ] İkinci pil, açma/kapama anahtarı, pil yüzdesi
- [ ] İki katlı plaket lehimi
- [ ] Kasa, damper ball, kamera aparatı
- [ ] Sürüş testi ve titreşim filtresi

## Lisans

[MIT](LICENSE)
