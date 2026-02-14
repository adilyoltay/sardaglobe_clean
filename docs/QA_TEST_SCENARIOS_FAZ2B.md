# Faz 2B QA Test Senaryoları
## Texture2DArray + PBO Integration Test Planı

---

## Senaryo 1: Açılış Testi (Startup Test)
**Amaç:** `useTexture2DArray=true` ile farklı zoom/tiling senaryosunda uygulama açılışı.

```gherkin
Given config.useTexture2DArray = true
  And config.usePboUploads = true
  And GL_TEXTURE_2D_ARRAY desteği mevcut
When uygulama başlatılır
  And kamera Earth'e zoom-in yapar (level 0 -> level 15)
Then TextureArrayManager başarıyla initialize olur
  And tier'lar oluşturulur (256x256, 512x512, vb.)
  And tile'lar layer'lara yüklenir
  And hiçbir GL_ERROR oluşmaz
  And 60 FPS altına düşülmez
```

**Başarı Kriterleri:**
- [ ] Uygulama crash olmadan açılır
- [ ] İlk frame'de tüm tile'lar placeholder ile görünür
- [ ] 5 saniye içinde gerçek tile'lar yüklenir
- [ ] GL debug output'ta hata yok

---

## Senaryo 2: Bleeding Regresyon Testi
**Amaç:** Farklı renkli komşu tile'larda bleeding regresyon doğrulaması.

```gherkin
Given 4 komşu tile yüklü
  And tile A: kırmızı renkli (255, 0, 0)
  And tile B: yeşil renkli (0, 255, 0)  
  And tile C: mavi renkli (0, 0, 255)
  And tile D: sarı renkli (255, 255, 0)
When useTexture2DArray = true modunda render edilir
Then tile kenarlarında (border) renk karışması (bleeding) olmamalı
  And her tile kendi rengini korur
  And UV koordinatları [0,1] aralığında kalır
  And atlas yokluğundan dolayı bleeding riski = 0%
```

**Başarı Kriterleri:**
- [ ] Kenar piksellerde komşu renklerine karışma yok
- [ ] Görsel olarak tile sınırları net
- [ ] Screenshot karşılaştırmasında pixel farkı < 0.1%

---

## Senaryo 3: Evict ve Layer Reuse Testi
**Amaç:** Memory baskısı altında layer boşaltma ve yeniden kullanım.

```gherkin
Given useTexture2DArray = true
  And maxLayersPerTier = 64 (düşük limit)
  And 100 tile yüklü (64'ten fazla)
When LRU eviction tetiklenir (64. tile yüklenirken)
Then en eski 37 tile'ın layer'ları FreeLayer ile serbest bırakılır
  And yeni tile'lar boşalan layer'lara atanır
  And freed layer'lar freeList'e eklenir
  And stats.totalRecycles > 0
  And serbest bırakılan tile'ların textureLayerHandle = -1 olur
```

**Başarı Kriterleri:**
- [ ] Memory sızıntısı yok (valgrind/asan ile doğrula)
- [ ] FreeLayer çağrıları başarılı
- [ ] Yeni tile'lar eski layer'ları kullanır
- [ ] Evicted tile'lar fallback texture'a döner

---

## Senaryo 4: Runtime Toggle Stabilite Testi
**Amaç:** `useTexture2DArray` runtime'da toggle edilebilirlik.

```gherkin
Given uygulama çalışıyor
  And 50 tile yüklü (bazıları array'de, bazıları atlas'ta)
When config.useTexture2DArray = false yapılır
  And 1 saniye beklenir
  And config.useTexture2DArray = true yapılır
Then toggle sırasında uygulama crash olmaz
  And mevcut tile'ların durumu korunur (veya gracefully resetlenir)
  And yeni tile'lar yeni modda yüklenir
  And render output'ta görsel glitch olmaz
```

**Başarı Kriterleri:**
- [ ] Toggle sırasında crash yok
- [ ] Görsel stabilite korunur
- [ ] FPS drop < 10% toggle anında
- [ ] Toggle sonrası tile'lar doğru modda render edilir

---

## Senaryo 5: Karma Render Pipeline Testi
**Amaç:** Düz tile + crossfade + heightmap eşzamanlı render.

```gherkin
Given useTexture2DArray = true
  And Tile A: normal tile (ready state)
  And Tile B: crossfade yapıyor (parent'tan child'a geçiş)
  And Tile C: heightmap ile (terrain displacement aktif)
When tüm tile'lar aynı frame'de render edilir
Then Tile A: layer index ile array'den render edilir
  And Tile B: crossfade blend değeri doğru uygulanır
  And Tile C: heightmap + displacement doğru çalışır
  And Hiçbir tile görsel artifact göstermez
  And Shader uniform'ları doğru set edilir
```

**Başarı Kriterleri:**
- [ ] Tüm tile tipleri doğru render edilir
- [ ] Crossfade blending sorunsuz
- [ ] Heightmap displacement aktif
- [ ] Shader switch'lerde performans drop yok

---

## Ekstra: PBO Async Completion Testi
**Amaç:** PBO callback mekanizmasının zamanlama tutarlılığı.

```gherkin
Given usePboUploads = true
  And 10 tile PBO upload queue'de
When ProcessUploads() çağrılır
  And GPU fence'ler signal edilir (1-2 frame sonra)
Then callback'ler doğru sırada çalışır
  And tile state Uploading -> Ready geçişi callback sonrası olur
  And mipmap generation callback içinde tamamlanır
  And tile.ClearPixels() sadece upload tamamlandıktan sonra çağrılır
```

**Başarı Kriterleri:**
- [ ] Callback'ler GPU completion sonrası fire edilir
- [ ] State geçişleri senkron
- [ ] Memory use-after-free yok
- [ ] Mipmaps hazır before texture kullanımı

---

## Test Ortamı Gereksinimleri

### Hardware
- GPU: GL 3.3+ ve GL_TEXTURE_2D_ARRAY desteği
- Memory: Minimum 4GB VRAM

### Config
```cpp
Config qaConfig;
qaConfig.useTexture2DArray = true;  // Test edilen özellik
qaConfig.usePboUploads = true;
qaConfig.pboUploadCount = 8;
qaConfig.pboUploadSize = 4 * 1024 * 1024;
qaConfig.textureAtlasEnabled = false;  // Force array path
qaConfig.maxTiles = 1000;
```

### Monitoring
- GL debug output (callback ile)
- FPS counter
- Memory profiler (valgrind massif veya benzeri)
- Screenshot comparison tool

---

## Test Sonuçları Formatı

Her senaryo için:
```
Senaryo X: [AD]
Durum: [PASS/FAIL/Partial]
Notlar: [Önemli gözlemler]
GL Errors: [Yok/Var -> detay]
FPS Impact: [% drop]
Memory Leak: [Yok/Var -> detay]
```

---

## QA Onayı

Tüm 5 senaryo **PASS** olduğunda:
- [ ] `useTexture2DArray` varsayılan olarak `true` yapılabilir
- [ ] Production deploy onayı verilir
- [ ] Dokümantasyon güncellenir

Tarih: _______________
QA Sorumlusu: _______________
