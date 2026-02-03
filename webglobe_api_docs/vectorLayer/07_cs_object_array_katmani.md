# CS_OBJECT_ARRAY Katmanı

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Verisi tiled olmayan yapılar için kullanılan, verileri toplu halde bir kere verdiğimiz katman tipidir.

## Point Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
    coords:[x0,y0,x1,y1,x2,y2],
    coordsZ:[z0,z1,z2],
    attribs:[{a0:"attribs"}, {a1:"attribs"}, {a2:"attribs"}],
  }]
```

## Line Nesnesinin Veri Yapısı
Line nesnesi çizmek için data yapısı aşağıdaki gibi verilmelidir. Multi line desteği vardır, multi'ler [[],[],[]] şeklinde, single line'lar []
şeklinde verilmelidir.
```javascript
const dataStructure =[{
  coords: [ /* multi*/ [[line], [line], [line], [line], [line]],  /*single*/[line] ],
	coordsZ: [ /* multi*/[[line], [line], [line], [line], [line]], /*single*/ [line] ],
	attribs: [{ a0: 'attribs' }, { a1: 'attribs' }]
  }]
```

## Polygon Nesnesinin Veri Yapısı
Polygon nesnesi çizmek için data yapısı aşağıdaki gibi verilmelidir. Multi polygon desteği vardır, multi'ler [[],[],[]] şeklinde, single line'lar []
şeklinde verilmelidir.
```javascript
const dataStructure =[{
    coords: [ /* multi*/ [[polygon], [polygon], [polygon], [polygon], [polygon]],  /*single*/[polygon] ],
	coordsZ: [ /* multi*/[[polygon], [polygon], [polygon], [polygon], [polygon]], /*single*/ [polygon] ],
	attribs: [{ a0: 'attribs' }, { a1: 'attribs' }]
  }]
```

## Shape Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
    coords:[x0,y0,x1,y1,x2,y2],
    coordsZ:[z0,z1,z2],
    attribs:[{a0:"attribs"}, {a1:"attribs"}, {a2:"attribs"}],
    typeProps: {
				shapeType: [shapeType1,shapeType2,shapeType3],
				radius1: [innerRadius1,innerRadius2,innerRadius3],
				radius2: [outerRadius1,outerRadius2,outerRadius3],
				rotDeg: [rotDeg1,rotDeg2,rotDeg3],
                stepAng : [stepAng1,stepAng2,stepAng3] //ellipse tipindeki nesneler için değer verilmeli, diğer tipler için null verilmeli
			}
  }]
```

## ArcArea Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
    coords:[x0,y0,x1,y1,x2,y2],
    coordsZ:[z0,z1,z2],
    attribs:[{a0:"attribs"}, {a1:"attribs"}, {a2:"attribs"}],
    typeProps: {
      radius1: [innerRadius1,innerRadius2,innerRadius3],
      radius2: [outerRadius1,outerRadius2,outerRadius3],
      startAng: [startAng1,startAng2,startAng3],
      endAng: [endAng1,endAng2,endAng3],
      stepAng: [stepAng1,stepAng2,stepAng3],
      showCenterLine : true,
    }
  }]
```

## OBJECT_3D Nesne Tipi

`CS_OBJECT_ARRAY` vektör layer tipleri için `objectType` parametresine `CSObjectTypes.OBJECT_3D` verilerek 3 boyutlu nesneler çizilebilir. Ayrıca `CSObjectTypes.OBJECT_3D` obje tipine özel, bir vektör layer içerisinde, birden çok farklı nesne tipi eklenebilir. Bu nesne tipi verilerek `point`, `line`, `polygon`, `ellipse`, `circle`, `rectangle`, `rectangle_bbox`, `arcArea`, `orbit`, `polyArc`, `corridor` ve `track` gibi nesneler 3 boyutlu olarak çizilebilir.

### Point3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.POINT,
      coords: [x0, y0, x1, y1, x2, y2],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      attribs: [ { a0: 'point0' }, { a0: 'point1' }, { a0: 'point2' }]
  }]
```

### Line3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
     shapeName: CSObject3DShapeTypes.LINE,
	 coords: [ /* multi*/ [[line], [line], [line]],  /*single*/[line] ],
	 zParams: {
	 	minZ: [ /* multiMinZ*/[[lineMinZ0], [lineMinZ1], [lineMinZ2]], /*singleMinZ*/ [lineMinZ3] ],
	 	maxZ: [ /* multiMaxZ*/[[lineMaxZ0], [lineMaxZ1], [lineMaxZ2]], /*singleMaxZ*/ [lineMaxZ3] ],
	 	minZMode: [/*multi minZMode*/ [lineMinZMode0, lineMinZMode1, lineMinZMode2], /*single minZMode*/lineMinZMode3],
	 	maxZMode: [/*multi maxZMode*/ [lineMaxZMode0, lineMaxZMode1, lineMaxZMode2], /*single maxZMode*/lineMaxZMode3]
	 },
	 attribs: [{ a0: 'line0' }, { a0: 'line1' }]
  }]
```

### Polygon3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
     shapeName: CSObject3DShapeTypes.POLYGON,
	 coords: [ /* multi*/ [[polygon], [polygon], [polygon]],  /*single*/[polygon] ],
	 zParams: {
	 	minZ: [ /* multiMinZ*/[[polygonMinZ0], [polygonMinZ1], [polygonMinZ2]], /*singleMinZ*/ [polygonMinZ3] ],
	 	maxZ: [ /* multiMaxZ*/[[polygonMaxZ0], [polygonMaxZ1], [polygonMaxZ2]], /*singleMaxZ*/ [polygonMaxZ3] ],
	 	minZMode: [/*multi minZMode*/ [polygonMinZMode0, polygonMinZMode1, polygonMinZMode2], /*single minZMode*/polygonMinZMode3],
	 	maxZMode: [/*multi maxZMode*/ [polygonMaxZMode0, polygonMaxZMode1, polygonMaxZMode2], /*single maxZMode*/polygonMaxZMode3]
	 },
	 attribs: [{ a0: 'polygon0' }, { a0: 'polygon1' }]
  }]
```

### Ellipse3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.ELLIPSE,
      coords: [x0,y0,x1, y1, x2, y2],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        radius1: [minRadius0, minRadius1, minRadius2],
        radius2: [maxRadius0, maxRadius1, maxRadius2],
        rotDeg: [rotDeg0, rotDeg1, rotDeg2],
        stepAng: [stepAng0, stepAng1, stepAng2]  
      },
      attribs: [ { a0: 'ellipse0' }, { a0: 'ellipse1' }, { a0: 'ellipse2' }]
  }]
```

### Circle3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.CIRCLE,
      coords: [x0,y0,x1, y1, x2, y2],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        radius: [radius0, radius1, radius2],
        stepAng: [stepAng0, stepAng1, stepAng2]
      },
      attribs: [ { a0: 'circle0' }, { a0: 'circle1' }, { a0: 'circle2' }]
  }]
```

### Rectangle3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.RECTANGLE,
      coords: [x0, y0, x1, y1, x2, y2],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        length: [length0, length1, length2],
        width: [width0, width1, width2],
        rotDeg: [rotDeg0, rotDeg1, rotDeg2]
      },
      attribs: [ { a0: 'rectangle0' }, { a0: 'rectangle1' }, { a0: 'rectangle2' }]
  }]
```

### Rectangle_BBOX3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.RECTANGLE_BBOX,
      coords: [[topLeft.x0,topLeft.y0, bottomRight.x0,bottomRight.y0],[topLeft.x1,topLeft.y1, bottomRight.x1,bottomRight.y1], [topLeft.x2,topLeft.y2, bottomRight.x2,bottomRight.y2]],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      attribs: [ { a0: 'rectangle_bbox0' }, { a0: 'rectangle_bbox1' }, { a0: 'rectangle_bbox2' }]
  }]
```

### ArcArea3D Nesnesinin Veri Yapısı
```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.ARCAREA,
      coords: [x0,y0,x1, y1, x2, y2],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        radius1: [innerRadius0, innerRadius1, innerRadius2],
        radius2: [outerRadius0, outerRadius1, outerRadius2],
        startAng: [startAngle0, startAngle1, startAngle2],
        endAng: [endAngle0, endAngle1, endAngle2],
        stepAng: [stepAngle0, stepAngle1, stepAngle2],
        showCenterLine : true,
      },
      attribs: [ { a0: 'arcArea0' }, { a0: 'arcArea1' }, { a0: 'arcArea2' }]
  }]
```

### Orbit3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.ORBIT,
      coords: [[firstCenter.x0,firstCenter.y0, secondCenter.x0,secondCenter.y0],[firstCenter.x1,firstCenter.y1, secondCenter.x1,secondCenter.y1], [firstCenter.x2,firstCenter.y2, secondCenter.x2,secondCenter.y2]],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        radius: [radius0, radius1, radius2]
      },
      attribs: [ { a0: 'orbit0' }, { a0: 'orbit1' }, { a0: 'orbit2' }]
  }]
```

>[!SCODE|label:Örnek Orbit Resmi|]

<img height="200" width="550" src="assets/img/orbit.png" data-origin="orbit.png" alt="Orbit Resim">

### PolyArc3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.POLYARC,
      coords: [ [[arcCenter.x0,arcCenter.y0], [polyCoords0]], [[arcCenter.x1,arcCenter.y1], [polyCoords1]], [[arcCenter.x2,arcCenter.y2], [polyCoords2]]],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        radius: [radius0, radius1, radius2]
        startAng: [startAngle0, startAngle1, startAngle2],
        endAng: [endAngle0, endAngle1, endAngle2],
        stepAng: [stepAngle0, stepAngle1, stepAngle2]
      },
      attribs: [ { a0: 'polyArc0' }, { a0: 'polyArc1' }, { a0: 'polyArc2' }]
  }]
```

>[!SCODE|label:Örnek PolyArc Resmi|]

<img height="380" width="500" src="assets/img/polyArc.png" data-origin="polyArc.png" alt="PolyArc Resim">

### Corridor3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.CORRIDOR,
      coords: [[corridorCoords0], [corridorCoords1], [corridorCoords2]],
      zParams: {
        minZ: [minZ0, minZ1, minZ2],
        maxZ: [maxZ0, maxZ1, maxZ2],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        width: [width0, width1, width2]
      },
      attribs: [ { a0: 'corridor0' }, { a0: 'corridor1' }, { a0: 'corridor2' }]
  }]
```

>[!SCODE|label:Örnek Corridor Resmi|]

<img height="500" width="500" src="assets/img/corridor.png" data-origin="corridor.png" alt="Corridor Resim">

### Track3D Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
      shapeName: CSObject3DShapeTypes.TRACK,
      coords: [[trackCoords0], [trackCoords1], [trackCoords2]],
      zParams: {
        minZ: [ [minZ0], [minZ1], [minZ2]],
        maxZ: [ [maxZ0], [maxZ1], [maxZ2]],
        minZMode: [minZMode0, minZMode1, minZMode2],
        maxZMode: [maxZMode0, maxZMode1, maxZMode2]
      },
      typeProps: {
        leftWidth: [leftWidth0, leftWidth1, leftWidth2],
        rightWidth: [rightWidth0, rightWidth1, rightWidth2]
      },
      attribs: [ { a0: 'track0' }, { a0: 'track1' }, { a0: 'track2' }]
  }]
```

>[!SCODE|label:Örnek Track Resmi|]

<img height="380" width="550" src="assets/img/track.png" data-origin="track.png" alt="Track Resim">


## Vektör Katman Datası Yenileme

`CSLayersTypes.CS_OBJECT_ARRAY` vektör katman tipi için desteklenir. Veriler, vektör katmanı eklendikten sonra `data` özelliğine verilerek `api_SetLayerData` metodu kullanılabilir.

```
CSObjectArrLayer.data = dataStructure

```

Vektör katmanı eklendikten sonra gelen datayı yenilemek için  `api_SetLayerData(CSObjectArrLayer, data, immediateUpdate)` metodu kullanılır.
```
  myGlobe.api_SetLayerData(CSObjectArrLayer, data, immediateUpdate)

```

|Data Parametreleri| Açıklama|
|-------------|-------------|
|`coords(number array)`       | Nesnelerin noktalarının longitude ve latitude değerleridir.(Derece)  |
|`coordsZ(number array)`       | 2 boyutlu nesne çizimlerinde kullanılır. Nesnelerin noktalarının yükseklik değerleridir. |
| `zParams` | 3 boyutlu nesne çizimlerinde kullanılır.|
| `typeProps` | Nesne tipine özel parametreler.|
|`attribs(object array)`       | `attribs`ler macrolar için kullanılabilir. |
