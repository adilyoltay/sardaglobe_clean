# Birim Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Birim Ayarlama Metodları

| Metod | Açıklama |
|-------|----------|
| `SetDistanceUnit(unitType)` | Mesafe ölçü birimini ayarlar |
| `SetAreaUnit(unitType)` | Alan ölçü birimini ayarlar |
| `SetAltitudeUnit(unitType)` | İrtifa ölçü birimini ayarlar |
| `SetAngleUnit(unitType)` | Açı ölçü birimini ayarlar |
| `SetVolumeUnit(unitType)` | Hacim ölçü birimini ayarlar |

## Birim Dönüşüm Metodları

| Metod | Açıklama |
|-------|----------|
| `DistanceUtoU(unitType, value, targetUnitType)` | Mesafe birimini dönüştürür |
| `AreaUtoU(unitType, value, targetUnitType)` | Alan birimini dönüştürür |
| `AltitudeUtoU(unitType, value, targetUnitType)` | İrtifa birimini dönüştürür |
| `AngleUtoU(unitType, value, targetUnitType)` | Açı birimini dönüştürür |
| `VolumeUtoU(unitType, value, targetUnitType)` | Hacim birimini dönüştürür |

## Kullanım Örnekleri

### Birim Ayarlama

```javascript
myGlobe.Units.SetDistanceUnit("kilometer")
myGlobe.Units.SetAreaUnit("hectare")
myGlobe.Units.SetAltitudeUnit("feet")
myGlobe.Units.SetAngleUnit("radyan")
myGlobe.Units.SetVolumeUnit("cubickilometer")
```

### Birim Dönüştürme

```javascript
// 100 metre -> santimetre
const cm = myGlobe.Units.DistanceUtoU("meter", 100, "centimeters")

// 10 metrekare -> dekar
const decare = myGlobe.Units.AreaUtoU("metersquare", 10, "decare")

// 1 inç -> metre
const meter = myGlobe.Units.AltitudeUtoU("inch", 1, "meter")

// 90 derece -> radyan
const rad = myGlobe.Units.AngleUtoU("degree", 90, "radyan")

// 1 metreküp -> dekametreküp
const dkm3 = myGlobe.Units.VolumeUtoU("cubicmeter", 1, "cubicdekameter")
```
