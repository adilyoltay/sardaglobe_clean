# RockMesh Parser — Kritik Bulgular (retroplasma Referans Karşılaştırması)

**Kaynak:** https://github.com/retroplasma/earth-reverse-engineering
**Tarih:** 2026-02-19
**Durum:** Parser'da 6 temel veri pipeline hatası tespit edildi. Mevcut mesh geometrisi hatalı.

---

## Özet

retroplasma/earth-reverse-engineering projesindeki C++ referans implementasyon (`client/rocktree_decoder.h`, `rocktree_ex.h`, `rocktree_gl.h`) ve proto şeması (`proto/rocktree.proto`) ile bizim `RockTreeNodeDataParser` karşılaştırıldı.

**Sonuç:** Mevcut parser ham protobuf byte'larını yanlış encoding ile yorumluyor. Üretilen mesh geometrisi, UV'ler ve index'ler hatalı.

---

## Bug 1: Vertex Position Encoding — YANLIŞ

### Bizim Parser (`rocktree_node_data_parser.cpp:170-184`)
```cpp
// Positions: int16 triplets
if (length % 6 != 0) { ... }           // 6 bytes per vertex (3 × int16)
out.positions.resize(length / 2);       // int16 count
memcpy(out.positions.data(), data + pos, length);  // Raw memcpy, NO delta decoding
```

### BuildMesh (`rockmesh_manager.cpp:1207`)
```cpp
double lx = parsed.positions[i * 3 + 0] / 32768.0;  // int16 → [-1, 1] range
```

### Doğru (retroplasma `rocktree_decoder.h:31-42`)
```cpp
auto count = packed.size() / 3;  // 3 bytes per vertex (3 × uint8)
uint8_t x = 0, y = 0, z = 0;
for (auto i = 0; i < count; i++) {
    vtx[i].x = x += data[count * 0 + i];  // Delta-encoded uint8
    vtx[i].y = y += data[count * 1 + i];  // Planar layout: [all X][all Y][all Z]
    vtx[i].z = z += data[count * 2 + i];  // Cumulative sum, uint8 wrapping
}
```

### Etki
- Vertex count yanlış hesaplanıyor (÷6 yerine ÷3)
- Position değerleri tamamen yanlış (delta decode yok, int16 yerine uint8)
- Sonuç: geometri garbage

---

## Bug 2: Field 11 Mapping — YANLIŞ

### Bizim Parser (`rocktree_node_data_parser.cpp:244`)
```cpp
} else if (fieldNum == 11) {
    // UV coordinates: uint16 pairs    ← YANLIŞ YORUM
    out.uv.resize(uvCount * 2);
    memcpy(out.uv.data(), data + pos, length);
}
```

### Proto Şeması (`rocktree.proto`)
```protobuf
message Mesh {
    optional bytes vertices = 1;
    optional bytes texture_coords = 2;      // ← Gerçek UV'ler BURADA
    optional bytes indices = 3;
    ...
    optional bytes texture_coordinates = 7;  // ← Alternatif UV format
    ...
    repeated float uv_offset_and_scale = 10;
    optional bytes normals = 11;             // ← Field 11 = NORMALS, UV DEĞİL!
}
```

### Etki
- Normals verisi UV olarak okunuyor → UV mapping tamamen bozuk
- Gerçek UV verileri (field 2 veya field 7) hiç parse edilmiyor

---

## Bug 3: UV Source — EKSİK

### Bizim Parser
- Field 2 (`texture_coords`) → **parse edilmiyor**
- Field 7 (`texture_coordinates`) → **parse edilmiyor**
- Field 10 (`uv_offset_and_scale`) → doğru parse ediliyor ama apply edecek UV yok

### Doğru (retroplasma `rocktree_decoder.h:46-62`)
```cpp
void unpackTexCoords(std::string packed, uint8_t* vertices, ...) {
    auto u_mod = 1 + *(uint16_t*)(data + 0);   // First 2 bytes: U modulus
    auto v_mod = 1 + *(uint16_t*)(data + 2);   // Next 2 bytes: V modulus
    data += 4;
    for (auto i = 0; i < count; i++) {
        vtx[i].u = u = (u + data[count*0+i] + (data[count*2+i] << 8)) % u_mod;
        vtx[i].v = v = (v + data[count*1+i] + (data[count*3+i] << 8)) % v_mod;
    }
    uv_offset = {0.5, 0.5};
    uv_scale = {1.0/u_mod, 1.0/v_mod};
}
```

### retroplasma integration (`rocktree_ex.h:populateNode`)
```cpp
unpackTexCoords(mesh.texture_coordinates(), ...);  // Field 7 önce
if (mesh.uv_offset_and_scale_size() == 4) {        // Field 10 override
    m.uv_offset[0] = mesh.uv_offset_and_scale(0);
    ...
} else {
    m.uv_offset[1] -= 1 / m.uv_scale[1];          // V flip fallback
    m.uv_scale[1] *= -1;
}
```

---

## Bug 4: Index Decoding — FARKLI ALGORİTMA

### Bizim Parser (`rocktree_node_data_parser.cpp:394-425`)
```cpp
uint32_t restartMarker = static_cast<uint32_t>(vertexCount) * 2;
// raw >= restartMarker → restart
// raw < restartMarker → vid = raw >> 1
```

### Doğru (retroplasma `rocktree_decoder.h:65-78`)
```cpp
for (int zeros = 0, a, b = 0, c = 0, i = 0; i < triangle_strip_len; i++) {
    int val = unpackVarInt(packed, &offset);
    triangle_strip[i] = (a = b, b = c, c = zeros - val);
    if (0 == val) zeros++;
}
```

### Etki
- Index'ler yanlış vertex'lere point ediyor
- Triangle strip → triangle list dönüşümü de farklı olabilir
- retroplasma `GL_TRIANGLE_STRIP` ile render ediyor, strip-to-triangle dönüşümü yapmıyor

---

## Bug 5: Normals — Mevcut Ama Kullanılmıyor

### Proto
```protobuf
message NodeData {
    optional bytes for_normals = 8;  // Shared normal palette (octahedral encoding)
}
message Mesh {
    optional bytes normals = 11;     // Indices into palette (uint16 per vertex)
}
```

### Doğru (retroplasma `rocktree_decoder.h:unpackForNormals`)
- `for_normals` field 8: 2-byte count + 1-byte scale + `count*2` bytes octahedral encoded
- Complex octahedral → XYZ decoding with sign reconstruction
- `normals` field 11: uint16 indices into the decoded palette → per-vertex normals

### Bizim Kod
- `for_normals` → hiç parse edilmiyor
- Field 11 → UV olarak okunuyor (Bug 2)
- Normals → BuildMesh'te cross product ile hesaplanıyor (hatalı outward test — düzeltildi)

---

## Bug 6: Octant/Layer Sistemi — EKSİK

### Doğru (retroplasma)
- Her vertex'te `octant` byte'ı (field 8 `layer_and_octant_counts` decode)
- `layer_bounds[3]` ile sadece terrain-visible üçgenler render ediliyor
- Octant mask uniform ile LOD geçişlerinde smooth blending

### Bizim Kod
- Octant sistemi hiç implemente edilmemiş
- Tüm üçgenler render ediliyor (water, overlay dahil)
- LOD geçişinde pop artifact'ları

---

## Vertex Format Karşılaştırma

| | Bizim | retroplasma (Doğru) |
|---|---|---|
| Position | int16 × 3 (6B) memcpy | uint8 × 3 (3B) delta-decoded |
| Octant | yok | uint8 (1B) |
| UV | field 11'den (YANLIŞ) | field 7'den, delta+modular (4B) |
| Stride | CPU float × 9 (36B) | GPU uint8 × 8 (8B) |
| Index | uint32 triangle list | uint16 triangle strip |
| Normals | cross product | GE pre-computed octahedral |
| Transform | CPU matrix multiply | GPU uniform |

---

## Düzeltme Planı

### Faz A: Parser Yeniden Yazımı (P0 — Zorunlu)
1. `unpackVertices()`: uint8 delta decode, planar layout
2. `unpackTexCoords()`: field 7 delta+modular decode, field 10 override
3. `unpackIndices()`: `zeros - val` triangle strip algorithm
4. `unpackForNormals()` + `unpackNormals()`: octahedral decode from field 8 + field 11
5. Field mapping düzeltmesi: field 2→UV (veya field 7), field 11→normals

### Faz B: BuildMesh Güncelleme (P0)
1. uint8 positions → transform matrix ile world-space'e dönüştür
2. GE normals varsa kullan, yoksa cross product fallback
3. UV'leri doğru kaynak (field 7 veya field 2) ve decode ile uygula
4. Octant/layer bounds ile triangle clipping

### Faz C: Render Path (P1)
1. Triangle strip rendering desteği (veya strip-to-list doğru dönüşüm)
2. Octant mask uniform desteği (LOD geçişleri)
3. uv_offset + uv_scale shader uniform'ları

### Bağımlılıklar
```
Faz A (parser) ──→ Faz B (BuildMesh) ──→ Faz C (render)
```
