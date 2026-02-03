# Kümeleme(Cluster) Katmanı Ekleme

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Katman nesnelerini kümelenmiş şekilde gösterebilmek için katman tipi `CS_OBJECT_ARRAY`, nesne tipi `POINT` ve katman değişkenlerinde `clusterStyle` array şeklinde tanımlanıp içerisinde en az bir kümeleme stili olmalıdır. Bu ön koşullar sağlanarak eklenen katmanlar kümeleme yapılarak çizilir.  Kümelenmeye dahil olmayan objeler katman `style` bölümü ile çizilirler. Örnekler için [bakınız.](/howto/?id=kümelemecluster-Örnekleri)

## Kümeleme stili

`api_GetDefaultClusterStyle(getInnerStyle)` metodu ile kümeleme stili oluşturulur.
`getInnerStyle` parametresi verilmez ise varsayılan cluster stilini döndürür. `true` verildiğinde varsayılan kümeleme stili değerleri ile olauşturulmuş iç stili verir.

Varsayılan kümeleme stili:
```javascript

const clusterStyle  = myGlobe.api_GetDefaultClusterStyle(false)

console.log(clusterStyle);
{
  rule: [true],
  cluster: {
    maxLod: 19,
    radius: 32,
    style: myGlobe.api_GetDefaultClusterStyle(true)
 }
}
```

|Parametre| Açıklama|
|-------------|-------------|
|`rule`       | Hangi objelerin cluster içine dahil edileceğini belirleyen filtre yapısı. |
|`cluster`    |             |
|             | `maxLod`:  Hangi loda kadar kümeleme yapılacağını belirler varsayılan `19` dur. |
|             | `radius`:  Kümeleme yaparken kullanılan yarıçap değeri, varsayılan `32` dir. |
|             | `style`:  Katman stili ile aynı değerlere sahiptir. |

Örnekler için [bakınız](/howto/?id=kümelemecluster-Örnekleri)

## Kümeleme Filtre Yapısı

Katman stil kurallarında kullanılan filtrelerde varsayılan macrolardan farklı olarak kümeleme işlemine özel macrolar kullanılır.

Özel macro yapısı aşağıdaki gibidir:

`Calc(resultType,process,attribName,digits)`

|      Parametre     |            Açıklama             |
|--------------------|---------------------------------|
|`resultType`        | İşlem sonucunda oluşan sonucun tipini belirtir. `NUMBER`, `STRING`, `INTEGER` tiplerini alabilir. |
|`process`           |    |
|                    | `objcount`: Kümeleme içindeki obje sayısını verir `attribName` değerine gerek yoktur.   |
|                    |  `max`: Kümeleme içindeki objelerin verilen `attribName` değerlerinden en büyük olanını döndürür. |
|                    |  `min`: Kümeleme içindeki objelerin verilen `attribName` değerlerinden en küçük olanını döndürür. |
|                    |  `avg`: Kümeleme içindeki objelerin verilen `attribName` değerlerinin ortalamasını döndürür. |
|                    |  `sum`:  Kümeleme içindeki objelerin verilen `attribName` değerlerinin toplamını döndürür. |
|                    |  `uniq`: Kümeleme içindeki objelerin verilen `attribName` değerlerinin kaç adet farklı değeri olduğunu döndürür.   |
| `attribName`       | objelerin `attribs` içindeki hangi değer ile işlem yapılacağını belirtir. |
| `digits         `  | `resultType` `NUMBER` veya `STRING` olduğunda virgülden sonra kaç basamak rakam olacağını belirtir.  |

Örnekler için [bakınız.](/howto/?id=kümelemecluster-Örnekleri)

## Kümeleme Alt Nesnelerine Erişim

Kümeleme objeleri kendi içlerinde alt objelerini de bulundurur. Kümeleme objesinde `clusterObject.obj.objects` konumunda alt objeler bir array içerisinde bulundurulur.

Örnekler için [bakınız.](/howto/?id=kümelemecluster-Örnekleri)
