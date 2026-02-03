# Plugin API Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Plugin Yönetim Metodları

| Metod | Açıklama |
|-------|----------|
| `api_RegisterPlugin(pluginObj, beforeObject)` | Plugin nesnesini küreye kaydeder |
| `api_UnRegisterPlugin(id)` | Plugin nesnesini küreden siler |
| `api_GetPlugin(id)` | ID'ye göre plugin nesnesini döndürür |
| `api_GetAllPluginsId()` | Tüm plugin ID'lerini array olarak döndürür |

## Plugin Standart Metodları

Plugin nesneleri içinde kullanılabilecek standart metodlar:

| Metod | Açıklama |
|-------|----------|
| `init(myGlobe, myGL)` | `api_RegisterPlugin` anında çağrılır. Globe ve GL referansları döner |
| `draw3D(projMatrix, modelMatrix, transPos)` | WebGL tabanlı 3D çizimlerde çağrılır |
| `draw2D()` | WebGL tabanlı 2D çizimlerde çağrılır |
| `mouseDown(x, y, event)` | Sol butona basıldığında çağrılır |
| `mouseMove(x, y, event)` | Mouse hareket ettirildiğinde çağrılır |
| `mouseUp(x, y, event)` | Mouse bırakıldığında çağrılır |
| `mouseClick(x, y, event)` | Tıklandığında çağrılır |
| `mouseDblClick(x, y, event)` | Çift tıklandığında çağrılır |
| `setGeometry()` | `api_SetGeometry` anında çağrılır |
| `free()` | `api_UnRegisterPlugin` ve `api_GlobeFree` anında çağrılır |

## Metod Detayları

### init(myGlobe, myGL)
Kürenin globe sınıfını ve WebGL context'ini döndürür. Bu sayede plugin içinde API metodları ve WebGL çizimleri kullanılabilir.

### draw3D(projMatrix, modelMatrix, transPos)
- `projMatrix`: Projeksiyon matrisi
- `modelMatrix`: Model matrisi
- `transPos`: Translate pozisyonu

### mouseDown/mouseMove/mouseUp
- `x, y`: Canvas üzerindeki mouse pozisyonu (screenX, screenY)
- `event`: Mouse event nesnesi
- `mouseDown`'dan `true` dönerse `mouseMove` ve `mouseUp` çağrılır

### setGeometry()
Harita geometrisi değiştiğinde (SPHERE ↔ FLAT) plugin nesnelerinin adapte olabilmesi için çağrılır.

### free()
Bellek sızıntısı olmaması için WebGL buffer'ları, shader programları vb. silinmelidir.
