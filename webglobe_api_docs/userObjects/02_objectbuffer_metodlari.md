# ObjectBuffer Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Yönetim Metodları

| Metod | Açıklama |
|-------|----------|
| `api_CreateObjectBuffer(bufferName, bufferId)` | ObjectBuffer oluşturur |
| `api_AddObjectBuffer(objectBuffer)` | ObjectBuffer ekler |
| `api_ObjectBufferCount()` | ObjectBuffer sayısını verir |
| `api_GetObjectBuffer(bufferIndex)` | Index'teki ObjectBuffer'ı verir |
| `api_FindObjectBufferById(bufferId)` | ID'ye göre ObjectBuffer'ı verir |
| `api_ExchangeObjectBuffer(i, j)` | İki ObjectBuffer'ın yerini değiştirir |
| `api_DeleteObjectBufferById(bufferId)` | ID'ye göre ObjectBuffer'ı siler |
| `api_DeleteObjectBufferByIndex(bufferIndex)` | Index'e göre ObjectBuffer'ı siler |
| `api_DeleteAllObjectBuffers()` | Tüm ObjectBuffer'ları siler |
| `api_GetNewObjectBufferId()` | Otomatik ID üretir |

## ObjectBuffer Instance Metodları

| Metod | Açıklama |
|-------|----------|
| `SetOpacity(opacity)` | Saydamlık değerini ayarlar |
| `AddObj(type, obj)` | Buffer'a nesne ekler |
| `GetTotalLimit()` | Nesnelerin toplam limitini verir |
| `GetObj(index)` | Index'teki nesneyi verir |
| `ObjCount()` | Nesne sayısını verir |
| `FindObjByRef(obj)` | Referansa göre nesne index'ini verir |
| `FindObjByFid(Fid)` | Fid'e göre nesneyi verir |
| `DeleteObjByRef(obj)` | Referansa göre nesneyi siler |
| `DeleteObjByFid(Fid)` | Fid'e göre nesneyi siler |
| `DeleteObjByIndex(index)` | Index'e göre nesneyi siler |
| `Clear(type)` | Belirtilen tipteki nesneleri siler |
| `SetActive(boolean)` | Görünürlüğü ayarlar |
| `GetActive()` | Aktiflik durumunu döndürür |
| `AddObjectsAsJSON(json)` | JSON'dan nesneler ekler |
| `GetObjectsAsJSON()` | Nesneleri JSON olarak döndürür |
| `ExchangeObj(i, j)` | İki nesnenin yerini değiştirir |
| `Delete()` | ObjectBuffer'ı siler |
| `CancelHighlights()` | Highlight'ları iptal eder |
| `CheckHighlights()` | Highlight timer'ı kontrol eder |
| `SetQuery(query)` | Sorgu özelliğini ayarlar |
| `TransformObjects(deltaLong, deltaLat)` | Nesneleri taşır |
| `SetReduction(iconObj, textObj)` | Seyreltme ayarlarını yapar |

## Reduction API Metodları

| Metod | Açıklama |
|-------|----------|
| `api_SetReductionBoxSize(value)` | Seyreltme kutusu boyutu. Varsayılan: 30 |
| `api_GetReductionBoxSize()` | Seyreltme kutusu boyutunu döndürür |
| `api_SetReductionPeriod(ms)` | Seyreltme periyodu. Varsayılan: 3000ms |
