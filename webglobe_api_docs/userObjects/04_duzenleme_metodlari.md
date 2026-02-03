# Düzenleme Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Nesne Oluşturma

| Metod | Açıklama |
|-------|----------|
| `api_ObjectCreator(type, isEdit, freeDraw)` | Yeni nesne oluşturur |
| `api_EditCallbackCreator()` | Varsayılan edit callback oluşturur |

## Düzenleme Metodları

| Metod | Açıklama |
|-------|----------|
| `api_StartEditObj(userObj, isNew, editobjCallback, isFreeDrawing)` | Nesneyi edit moduna alır |
| `api_StopEditObj(isNewDelete)` | Edit modundan çıkarır |
| `api_CancelEditObj()` | Düzenlemeyi iptal eder |
| `api_SetEditingObjParams(editObjParams)` | Edit parametrelerini değiştirir |

## Nesne Instance Metodları

| Metod | Açıklama |
|-------|----------|
| `setActive(isOn)` | Nesne aktifliğini değiştirir |
| `getAsJSON()` | Nesneyi JSON olarak dışa aktarır |
| `rebuild()` | Koordinat değişikliklerini uygular |
| `styleChanged()` | Stil değişikliklerini uygular |
| `delete()` | Nesneyi siler |
| `objectBuffer` | Nesnenin bulunduğu ObjectBuffer referansı |

## Icon Metodları

| Metod | Açıklama |
|-------|----------|
| `api_GetShapeTypes()` | Şekil tiplerini ve isimlerini verir |
| `api_GetShapeBitmap(shapeType, borderColor, fillColor, borderWidth, backGroundColor, WPixel, HPixel)` | Şekil bitmap'i oluşturur |

## Çizgi Tipi Ekleme

| Metod | Açıklama |
|-------|----------|
| `api_AddCustomLineTypes(lineTypeArr)` | Özel çizgi tipleri ekler |

### Özel Çizgi Tipi Örneği

```javascript
myGlobe.api_AddCustomLineTypes([
  {
    id: 'DashedLine2',
    info: 'Kesikli Çizgi 2',
    layer: [
      {
        type: CSPatternTypes.LINE,
        pattern: ['F', 'E'],
        fillWidth: 1.0,
        emptyWidth: 3.0
      }
    ]
  }
])
```

## Düzenleme Örneği

```javascript
const objectBuffer = myGlobe.api_CreateObjectBuffer('sample', myGlobe.api_GetNewObjectBufferId())
myGlobe.api_AddObjectBuffer(objectBuffer)

// Yeni çizgi oluştur ve edit moduna al
const isNew = true
const freeDraw = false
const line = myGlobe.api_ObjectCreator(CSObjectTypes.LINE, isNew, freeDraw)
line.style = myGlobe.api_GetDefaultStyle()
objectBuffer.AddObj(CSObjectTypes.LINE, line)

const editCallback = myGlobe.api_EditCallbackCreator()
myGlobe.api_StartEditObj(line, true, editCallback, freeDraw)
```
