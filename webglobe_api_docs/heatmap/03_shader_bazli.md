# Shader Bazlı Isı Haritası

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Kullanıcının verdiği değerlere göre ısı haritaları, `CSHeatmapTypes.POINT`, `CSHeatmapTypes.SQUARE_GRID` ya da `CSHeatmapTypes.HEXAGON_GRID` nesne tipi olarak, deniz seviyesinde çizilir.

Shader bazlı çizim tekniği için, ısı haritası metodları `myGlobe.Heatmap` sınıfından çağrılarak kullanılır.

## Metodlar

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `Add(heatmap, beforeObject)`| Isı haritası ekler |
| `Delete(heatmapOrId)`| Isı haritası siler |
| `DeleteAll()`| Tüm ısı haritalarını siler |
| `SetRadius(heatmap, radius)`| Yarıçapı değiştirir |
| `SetColorPalette(heatmap, colorPalette)`| Renk paletini değiştirir |
| `StyleChanged(heatmap)`| Stili günceller |
| `SetOpacity(heatmap, opacity)`| Saydamlığı değiştirir |
| `SetDepthTest(heatmap, depthTest)`| Derinlik testini ayarlar |
| `Get(id)`| ID'ye göre ısı haritası döndürür |
| `GetAll()`| Tüm ısı haritalarını döndürür |
| `GetNewId()`| Otomatik ID üretir |
| `SetOn(heatmap, on)`| Aktifliği değiştirir |
| `GetOn(heatmap)`| Aktiflik bilgisini döndürür |
| `ObjCount()`| Nesne sayısını verir |
| `GetDefaultPointStyle()`| Varsayılan point stilini verir |
| `GetDefaultGridStyle()`| Varsayılan grid stilini verir |
| `GetDefaultGridHoverStyle()`| Varsayılan grid hover stilini verir |

## Type Tipleri

|  type   | Açıklama|
|-------------------|------------|
|  `POINT`          | Noktalar olarak çizilir |
|  `SQUARE_GRID`    | Kare polygon'lar olarak çizilir |
|  `HEXAGON_GRID`   | Hexagon polygon'lar olarak çizilir |

```javascript
const CSHeatmapTypes = {
  POINT,
  SQUARE_GRID,
  HEXAGON_GRID
}
```

## Unit Tipleri

|  unit   | Açıklama|
|-------------------|------------|
|  `PIXEL`          | Piksel bazlı |
|  `METER`          | Metre bazlı |

```javascript
const CSHeatmapUnitTypes = {
  PIXEL,
  METER
}
```

## Renk Tipleri

- **RGB**: `rgb(255,255,255)`
- **HEX long**: `#ffffff`
- **HEX short**: `#FFF`

## Örnek - Point Tipi

```javascript
const { Heatmap } = myGlobe

const heatmap = {
  id: Heatmap.GetNewId(),
  type: CSHeatmapTypes.POINT,
  data: {
    coords: [long1, lat1, long2, lat2, ...],
  },
  style: Heatmap.GetDefaultPointStyle(),
  startLod: 2,
  endLod: 19
}

Heatmap.Add(heatmap)
```

## Örnek - Grid Tipi

```javascript
const { Heatmap } = myGlobe

const heatmap = {
  id: Heatmap.GetNewId(),
  type: CSHeatmapTypes.HEXAGON_GRID,
  data: {
    coords: [long1, lat1, long2, lat2, ...],
  },
  style: Heatmap.GetDefaultGridStyle(),
  hoverStyle: Heatmap.GetDefaultGridHoverStyle(),
  startLod: 2,
  endLod: 19
}

Heatmap.Add(heatmap)
```

## Detaylı Örnekler

### Point Tipi - Piksel

```javascript
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.POINT,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 16,
    weight: 1,
    unit: CSHeatmapUnitTypes.PIXEL,
    opacity: 1,
    colorPalette: [
      { color: 'rgb(0, 0, 0)', density: 0 },
      { color: 'rgb(0, 0, 255)', density: 0.33 },
      { color: 'rgb(255, 255, 0)', density: 0.66 },
      { color: 'rgb(255, 0, 0)', density: 1.0 }
    ]
  },
  startLod : 2,
  endLod: 19
})
```

### Point Tipi - Metre

```javascript
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.POINT,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 100,
    weight: 1,
    unit: CSHeatmapUnitTypes.METER,
    opacity: 1,
    colorPalette: [
      { color: 'rgb(0, 0, 0)', density: 0 },
      { color: 'rgb(0, 0, 255)', density: 0.33 },
      { color: 'rgb(255, 255, 0)', density: 0.66 },
      { color: 'rgb(255, 0, 0)', density: 1.0 }
    ],
    maxRadius: 1024
  },
  startLod : 2,
  endLod: 19
})
```

### Hexagon Grid - 2D

```javascript
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.HEXAGON_GRID,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 200, // metre
    opacity: 1,
    colorPalette: ['#ffffb2', '#fed976', '#feb24c', '#fd8d3c', '#f03b20', '#bd0026'],
    extrude: false,
    border: true,
    polyWithNoData: null,
    depthTest: false
  },
  startLod : 2,
  endLod: 19
})
```

### Hexagon Grid - 3D (Extrude)

```javascript
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.HEXAGON_GRID,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 200, // metre
    opacity: 1,
    colorPalette: ['#ffffb2', '#fed976', '#feb24c', '#fd8d3c', '#f03b20', '#bd0026'],
    extrude: true,
    maxHeight: 2000,
    minHeight: 100,
    applyLight: true,
    depthTest: true
  },
  startLod : 2,
  endLod: 19
})
```

### Square Grid - 2D

```javascript
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.SQUARE_GRID,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 200, // metre
    opacity: 1,
    colorPalette: ['#ffffb2', '#fed976', '#feb24c', '#fd8d3c', '#f03b20', '#bd0026'],
    extrude: false,
    depthTest: false
  },
  startLod : 2,
  endLod: 19
})
```

### HoverStyle Örneği

```javascript
const hoverStyle = myGlobe.Heatmap.GetDefaultGridHoverStyle()
hoverStyle.label.backgroundStyle = { box:true, filled:true, thickness:3, fillOpacity:0.5 }
myGlobe.Heatmap.Add({
  type: CSHeatmapTypes.HEXAGON_GRID,
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 200,
    opacity: 1,
    colorPalette: ['#ffffb2', '#fed976', '#feb24c', '#fd8d3c', '#f03b20', '#bd0026'],
    extrude: true,
    maxHeight: 2000,
    minHeight: 100,
    applyLight: true,
    depthTest: true
  },
  hoverStyle,
  startLod : 2,
  endLod: 19
})
```

>[!WARNING]
>Rasterize bazlı çizilen ısı haritası 3d, shader bazlı çizilen heatmap'lerde `CSHeatmapTypes.POINT` tipi 2d, `CSHeatmapTypes.SQUARE_GRID` ve `CSHeatmapTypes.HEXAGON_GRID` tipleri ise 3d kategorisinde değerlendirilir.
