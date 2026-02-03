# User Objects (Kullanıcı Nesneleri)

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Kullanıcı nesneleri, kullanıcı tarafından Globe'a verilen geometrik nesnelerdir. Bu nesneler **ObjectBuffer** nesnesi ile yönetilir.

## Desteklenen Nesne Tipleri (CSObjectTypes)

| Tip | Açıklama |
|-----|----------|
| `POINT` | Nokta nesnesi |
| `LINE` | Çizgi nesnesi |
| `POLYGON` | Çokgen nesnesi |
| `SHAPE` | Şekil nesnesi (daire, elips, yıldız vb.) |
| `MODEL` | 3D model nesnesi |
| `ARCAREA` | Arc alan nesnesi |

## ObjectBuffer

ObjectBuffer, Globe'a eklenecek nesnelerin çizilmesi için gerekli işlemlerin yapıldığı nesnedir:
- Ekleme
- Düzenleme
- Silme
- Listeleme
- Arama

## Temel Kullanım

```javascript
// ObjectBuffer oluştur
const myObjectBuffer = myGlobe.api_CreateObjectBuffer(
  "objectBufferName",
  myGlobe.api_GetNewObjectBufferId()
)

// Globe'a ekle
myGlobe.api_AddObjectBuffer(myObjectBuffer)

// Nesne ekle
const pointObj = myGlobe.api_ObjectCreator(CSObjectTypes.POINT, false, false)
pointObj.coords = [32.0, 40.0]
pointObj.style = myGlobe.api_GetDefaultStyle()
myObjectBuffer.AddObj(CSObjectTypes.POINT, pointObj)
```
