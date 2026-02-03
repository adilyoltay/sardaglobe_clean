# Katman Filtre Yapısı

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Filtre Yapısı
Filtre yapısı bir kolon adı, bir sabit değer ve bir operatörden oluşur. Kolondan gelen değer ile kullanıcının verdiği sabit değer operatöre göre işleme tabi tutulur, işlem sonucu `true` ise nesne bu kurala uyduğu için katmanda görünür, `false` ise görünmez.

>[!SCODE|label: Tekli filtre yapısı aşağıdaki gibidir|]

```javascript  
Layer.filter = [ 'all/any', [condition, value1, value2] ]
```

>[!SCODE|label: Çoklu filtre yapısı aşağıdaki gibidir|]

```javascript  
Layer.filter = [ 'all/any', [condition, value1, value2], [condition, value1, value2], ... ]
```

>[!SCODE|label: İç İçe filtre yapısı aşağıdaki gibidir|]

```javascript  
Layer.filter = [ 'all/any', [
                              'all/any', [condition, value1, value2],  
                              ['all/any', [condition, value1, value2], [condition, value1, value2] ]
                            ],...
               ]
```

| Parametre | Değerler  |
|-------------|--------|
|`any`| Verilen filtrelerden en az biri kurala uyuyorsa sonuç olarak `true` döner ve nesne çizilir. |
|`all`| Verilen filtrelerin tamamı kurala uyuyorsa sonuç olarak `true` döner ve nesne çizilir. |
|`value1`| Nesne özniteliklerinden gelen bir değer olabilir. String tipinde verilmelidir.|
|`value2`| Sabit bir değer olabilir. String veya Number tipinde değerler alabilir.|
| `condition`| `value1`in `value2`ye göre durumunu dikkate alan bir operatördür. operatörler:|
||    `LESS` -  `<`: Küçüktür ||
||    `GREATER` - `>` : Büyüktür||
||    `LESSEQUAL` - `<=` : Küçük Eşittir||
||    `GREATEREQUAL` -  `>=` : Büyük Eşittir||
||    `EQUAL` -        `==` :  Eşittir||
||    `NOTEQUAL` -     `!=` : Eşit Değildir||
||    `CONTAINS` -     `in` : Kapsar/ İçerir.||
||    `NOTCONTAINS` -  `!in` : Kapsamaz/İçermez.||
||    `HAS` -  `has` : verilen kolon adı gelen attribs listesinde var mı||
||    `NOTHAS` -  `!has` : verilen kolon adı gelen attribs listesinde yok mu||

## Filtreler 3 çeşit olabilir.
- Tekli Filtre
- Çoklu Filtre
- İç İçe Filtre


>[!SCODE|label: LESSEQUAL İle Örnek Tekli Filtre|]

```javascript  
Layer.filter = [ "any", ["<", "attrbKey1", 50.5] ]
//attrbKey1 kolonundaki değeri 50.5'ten küçük olan nesneler seçilir.
```

>[!SCODE|label: EQUAL İle Örnek Tekli Filtre|]

```javascript  
Layer.filter = [ "any", ["EQUAL", "attrbKey1", 50] ]
//attrbKey1 kolonundaki değeri 50 olan nesneler seçilir.
```

>[!SCODE|label:in İle Örnek Tekli Filtre 1|]

```javascript  
Layer.filter = [ "any", ["in", "ilAdi", "Ankara"] ]
// `ilAdi` kolonunun içerisinde `Ankara` kelimesi geçen nesneler katmana eklenir.
```

>[!SCODE|label:in İle Örnek Tekli Filtre 2|]

```javascript  
Layer.filter = [ "any", ["in", "attrbKey1", "Ankara", "İstanbul", "İzmir"]  ]
// `attrbKey1` kolonunun içerisinde `Ankara`, `İstanbul` ya da `İzmir` kelimelerinden biri geçiyorsa nesneler katmana eklenir.

```

>[!SCODE|label:has İle Örnek Tekli Filtre|]

```javascript  
Layer.filter = [ "all", ["has", "id"] ]
// `id` kolonu gelen attribs listesinde varsa nesneler katmana eklenir.
```

>[!SCODE|label: Çoklu Filtre Kullanım Örneği|]

```javascript  
Layer.filter = [
  "all",
  [ "==", "attrKey1", "İstanbul" ] ,
  [ "<", "attrbKey2", 200 ]
 ]
 //attrbKey1 değeri İstanbul olanlar
 //ve
 //attrbKey2 değeri 200'den küçük olan nesneler seçilir.

```

>[!SCODE|label: İç İçe Filtre Kullanım Örneği|]

```javascript  
Layer.filter = [
  "all",
  ["in", "ilAdi", "m", "a"]  ,
  [ 'any', ["<", "nüfus", 5000000], [">", "nüfus", 10000000] ]
 ]
 //ilAdi içerisinde `m` ya da `a` harflerinden biri geçiyorsa
 //ve
 //nüfus'u 5 milyondan küçük olanlar veya nüfusu 10 milyondan büyük olan nesneler seçilir

```

## ObjectParams Örnekleri:

### Nokta Nesnesi:

|Değer| tipi|
|------|--------|
|`Fid`|macro|
|`X`| x koordinatı, macro veya sabit number değer|
|`Y`| y koordinatı, macro veya sabit number değer|
|`coordsZ`| noktanın yüksekliği, macro veya sabit number değer|

### Çizgi Nesnesi:

|Değer| tipi|
|------|--------|
|`Fid`|macro|
|`coordsZ`| noktanın yüksekliği, macro veya sabit değer|

### Alan Nesnesi:

|Değer| tipi|
|------|--------|
|`Fid`|macro|
|`coordsZ`| noktanın yüksekliği, macro veya sabit değer|
|`heights`| macro veya sabit array: [number,number,...] şeklinde |
