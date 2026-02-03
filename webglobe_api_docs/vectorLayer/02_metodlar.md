# Metodlar

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `api_AddLayer(Layer, beforeObject)`        |   Katmanı `beforeObject`in altında çizer. `beforeObject`, herhangi bir raster, raster overlay, object array, vektör katman ya da plugin nesnesi olabilir. `beforeObject` parametresi verilmediğinde, eklenecek katman; küre üzerinde bulunan raster, raster overlay, object array, vektör katman ve plugin'lerden sonra çizilir. |
| `api_GetLayer(index or displayName)`              |   Kullanıcı isterse bir index verip isterse de displayName verip katmanı alabilir. Eğer `index` verirse o index'te bulunan katman döner, `displayName` verirse o displayName'e sahip katman döner.|
| `api_UpdateLayer(index, layerObj)`     |   `index`i verilen vektör katmanına yeni oluşturulan `layerObj`u verir ve bu `layerObj` içindeki değerlere göre vektör katmanını günceller.|
| `api_DeleteLayer(Layer)`     |   Verilen katman nesnesini siler.|
| `api_DeleteLayers()`         |   Küreye eklenmiş bütün vektör katman nesnelerini siler.|
| `api_ReloadLayer(Layer)`       |   Verilen katmanı yeniden yükler.|
| `api_LayerCount()`             |   Katman sayısını verir. |
| `api_SetLayerOpacity(Layer, value)`       |   Verilen katmanın stillerinin saydamlık değerini değiştirir, böylece bütün nesnenin saydamlık değeri değişir. `0<=value<=1` |
| `api_SetLayerOn(Layer,On)`       |   Verilen katmanın aktifliğini değiştirir.|
| `api_GetLayerOn(Layer)`       |   Verilen katmanın aktif olup olmadığı bilgisini verir.|
| `api_SetReductionBoxSize(value)`       |   `iconReduction` ya da `textReduction` özelliği `true` olan kullanıcı nesneleri ya da layer'lar için kaç piksel'de bir seyreltme yapılacağını değiştirir. Varsayılan değeri 30'dur.|
| `api_GetReductionBoxSize()`       |   `iconReduction` ya da `textReduction` özelliği `true` olan kullanıcı nesneleri ya da layer'lar için kaç piksel'de bir seyreltme yapıldığı bilgisini verir.|
| `api_SetReductionPeriod(ms)`       | Parametre olarak milisaniye alır, `iconReduction` ya da `textReduction` özelliği `true` olan kullanıcı nesneleri ya da layer'lar için kullanılır, seyreltme zaman periyodunu ayarlar. Varsayılan değeri 3000'dir. |
| `api_LayerObjectToJSON(obj)`       | Verilen layer objesini JSON olarak döndürür.  |
| `api_GetDefaultLayerStyle()`| Varsayılan layer stilini verir. |
| `api_GetLayerStyle(layer)`       | Verilen `layer` objesinin `style`ını JSON olarak döndürür.  |
| `api_LayerStyleChanged(Layer)`       |   Verilen katmanın stilini günceller.|
| `api_GetLayerLimitsOfLoadedObjects(layer)`       | Verilen `layer`ın yüklü nesnelerinin limitlerini derece cinsinden verir. Upper Right, Lower Left. ```  { ur:{ x, y},  ll:{ x, y}}```  |
| `api_GetDefaultClusterStyle(getInnerStyle)`| varsayılan kümeleme stili verir. `getInnerStyle` `true` verildiğinde kümeleme için varsayılan iç stil verir [bakınız](/vectorLayer/?id=kümeleme-stili) |
| `api_GeoJSONToObjectArrData(geoJSONData)`| `geoJSONData`sını `CS_OBJECT_ARRAY` katman tipinde kullanılan data cinsinden verir. `{pointData, lineData, polygonData}` Örnek için [bakınız](/howto/?id=geojson-datasını-cs_object_array-datasına-Çevirme) |
| `api_LoadVectorLayerWhileScreenIsMoving(boolean)`| `MVT_XYZ` tipindeki layer'ların ekran hareket halindeyken yüklenip yüklenmeyeceğini değiştirir. `boolean` değeri `true` ise layer'lar, ekran hem hareket halindeyken hem de değilken yüklenir, `boolean` değeri `false` ise layer'lar, sadece ekran hareket halinde değilse yüklenir. Varsayılan değeri `false`tur. |
| `api_GetDefaultCompositeLayerStyle()`| Varsayılan composite layer stilini verir. |
| `api_SetVectorLayerTimeOut(ms)`|Vektör layer'ların timeout süresini değiştirir. Değerler `milisaniye` cinsinden verilmelidir. Varsayılan değeri 15000 milisaniye(15 sn)'dir.|
| `api_ReTryAtVectorLayerTimeout(boolean, callback)`| `timeout`a ya da `error`a düşen vektör layer istekleri için yeniden indirme isteği yapılıp yapılmayacağı durumunu değiştirir. |
| `api_GetTotalLayersAsJSON()`| Küre üzerindeki tüm raster ve vektör katmanlarını JSON olarak döndürür. |
| `api_AddTotalLayers(json)`| `json` verilerek küreye hem toplu olarak hem de teker teker raster ve vektör katmanları eklenebilir. |
| `api_SetLayerFlash(layer,styleOrMVTXYZName, flashIcon,flashLabels,flashGeo,flashShape)`|Seçim modunda olmayan nesnelerin yanıp sönme değerlerini değiştirip küre üzerinde uygular. |
| `api_SetFlashPeriod(ms)`       | Parametre olarak milisaniye alır, nesnelerin yanıp sönme periyodunu ayarlar. Varsayılan değeri 800'dür. |
| `api_GetFlashPeriod()`       | Nesnelerin yanıp sönme periyot değerini verir. |
| `api_UpdateLayerData(Layer, process, data)` | `CSLayersTypes.CS_OBJECT_ARRAY` vektör katman tiplerinde, `point` nesne tipi için geçerlidir. `Layer`ın `data`sını güncelleyerek küre üzerinde uygular. |
| `api_GetLayerById(id)`              |   Verilen `id`ye sahip vektör katmanını döndürür. |
| `api_GetNewLayerId()`              |   Vektör katmanları için otomatik id üretir. |
| `api_CanMoveLabelsByMouse(canMove)`| Tüm vektör katman ve track nesneleri için geçerli global bir metodtur. |
| `api_CanResetLabelsByMouse(canReset)`|Tüm vektör katman ve track nesneleri için geçerli global bir metodtur. |
| `api_ResetAllLayerLabels()`|Tüm vektör katman nesnelerinin yazılarının pozisyonunu resetler ve yazıları orjinal yerine çeker.|
