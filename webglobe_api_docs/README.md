# WebKüre API Modüler Dokümantasyon

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Bu klasör, WebKüre API dokümantasyonunun modüler yapısını içerir.

## Klasör Yapısı

```
webglobe_api_docs/
├── WebKure_API_Documentation.md    # Ana dokümantasyon dosyası
├── README.md                        # Bu dosya
│
├── introduction/                    # Giriş ve Kurulum
│   ├── 01_kurulum.md
│   ├── 02_globe_parametreleri.md
│   └── 03_globe_komutlari.md
│
├── navigation/                      # Navigasyon ve Kamera
│   ├── 01_tanim.md
│   ├── 02_kamera_animasyon_islemleri.md
│   ├── 03_kamera_kontrol_islemleri.md
│   ├── 04_kamera_olcum_islemleri.md
│   ├── 05_kamera_uzaklik_islemleri.md
│   ├── 06_kamera_ileri_geri_alma.md
│   ├── 07_globe_yon_islemleri.md
│   ├── 08_mouse_islemleri.md
│   ├── 09_mouse_navigasyon_islemleri.md
│   ├── 10_kamera_yonetimi.md
│   └── 11_ornekler.md
│
├── rasterLayer/                     # Raster Katmanları (YENİ)
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   ├── 03_raster_yapisi.md
│   └── 04_loddisplay_supporturl.md
│
├── rasterOverlay/                   # Raster Overlay (YENİ)
│   ├── 01_tanim.md
│   ├── 02_image_overlay.md
│   └── 03_wms_overlay.md
│
├── vectorLayer/                     # Vektör Katmanları
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   ├── 03_secim_metodlari.md
│   ├── 04_vektor_katman_nesne_metodlari.md
│   ├── 05_vektor_katman_ekleme.md
│   ├── 06_mvt_xyz_composite_layer.md
│   ├── 07_cs_object_array_katmani.md
│   ├── 08_kumeleme_katmani.md
│   ├── 09_katman_filtre_yapisi.md
│   ├── 10_vektor_cizim_stilleri.md
│   ├── 11_secim_cizim_stilleri.md
│   └── 12_askeri_semboller.md
│
├── objectArray/                     # Object Array
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   ├── 03_nesne_ekleme.md
│   └── 04_cizim_stilleri.md
│
├── userObjects/                     # Kullanıcı Nesneleri (YENİ)
│   ├── 01_tanim.md
│   ├── 02_objectbuffer_metodlari.md
│   ├── 03_nesne_tipleri.md
│   └── 04_duzenleme_metodlari.md
│
├── track/                           # Track (YENİ)
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   ├── 03_track_yapisi.md
│   └── 04_cizim_stilleri.md
│
├── heatmap/                         # Isı Haritaları
│   ├── 01_tanim.md
│   ├── 02_rasterize_bazli.md
│   └── 03_shader_bazli.md
│
├── plugin/                          # Plugin (YENİ)
│   ├── 01_tanim.md
│   ├── 02_api_metodlari.md
│   └── 03_ornek.md
│
├── screen/                          # Ekran İşlemleri (YENİ)
│   ├── 01_tanim.md
│   ├── 02_koordinat_islemleri.md
│   ├── 03_mouse_islemleri.md
│   ├── 04_cizim_islemleri.md
│   └── 05_obje_tespit.md
│
├── coordinates/                     # Koordinat Sistemleri
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   └── 03_koordinat_donusumleri.md
│
├── analysis/                        # Analiz İşlemleri
│   ├── 01_gorus_analizi.md
│   ├── 02_yukseklik_profil.md
│   ├── 03_iki_nokta_gorunurluk.md
│   └── 04_gunes_ay_isiklandirma.md
│
├── drawOrder/                       # Çizim Sırası
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   └── 03_ornekler.md
│
├── math/                            # Matematik Kütüphanesi
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   └── 03_ornekler.md
│
├── units/                           # Birimler (YENİ)
│   ├── 01_tanim.md
│   ├── 02_metodlar.md
│   └── 03_birim_tipleri.md
│
├── language/                        # Dil Ayarları
│   └── 01_dil_ayarlari.md
│
└── errorlog/                        # Hata Kayıtları
    └── 01_hata_kayitlari.md
```

## Dosya Sayıları

| Klasör | Dosya Sayısı |
|--------|-------------|
| introduction | 3 |
| navigation | 11 |
| rasterLayer | 4 |
| rasterOverlay | 3 |
| vectorLayer | 12 |
| objectArray | 4 |
| userObjects | 4 |
| track | 4 |
| heatmap | 3 |
| plugin | 3 |
| screen | 5 |
| coordinates | 3 |
| analysis | 4 |
| drawOrder | 3 |
| math | 3 |
| units | 3 |
| language | 1 |
| errorlog | 1 |
| **Toplam** | **74** |

## Yeni Eklenen Modüller

- **rasterLayer/** - Raster katman yönetimi (XYZ_MERCATOR, WMS)
- **rasterOverlay/** - Image ve WMS overlay işlemleri
- **userObjects/** - Kullanıcı nesneleri ve ObjectBuffer yönetimi
- **track/** - Yüksek performanslı nokta seti çizimi
- **plugin/** - Özel WebGL plugin geliştirme
- **screen/** - Ekran, mouse, klavye ve obje tespit işlemleri
- **units/** - Birim dönüşümleri (mesafe, alan, açı, hacim)

## Dosya Adlandırma Kuralları

- Tüm dosyalar `XX_dosya_adi.md` formatında numaralandırılmıştır
- Türkçe karakterler ASCII karakterlere dönüştürülmüştür (ö→o, ü→u, ş→s, ı→i, ğ→g, ç→c)
- Boşluklar alt çizgi (_) ile değiştirilmiştir
- Tüm harfler küçük harftir
