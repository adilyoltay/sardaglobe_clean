# Plugin Örneği

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Dikdörtgen Çizen Plugin

```javascript
const rectanglePlugin = {
  id: 'rectangle',
  globe: null,
  gl: null,
  shaderProgram: null,
  vertexBuffer: null,

  // Standart Metodlar
  init: function(myGlobe, myGL) {
    this.globe = myGlobe
    this.gl = myGL
  },

  draw3D: function(projMatrix, modelMatrix, transPos) {
    const { gl } = this
    gl.useProgram(this.shaderProgram)
    gl.disable(gl.DEPTH_TEST)
    gl.enable(gl.BLEND)
    
    gl.uniformMatrix4fv(this.projectionMatrixLoc, false, projMatrix)
    gl.uniformMatrix4fv(this.modelViewMatrixLoc, false, modelMatrix)
    
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer)
    gl.vertexAttribPointer(this.vertexPosLoc, 3, gl.FLOAT, false, 0, 0)
    gl.drawArrays(gl.LINE_LOOP, 0, 4)
    
    gl.enable(gl.DEPTH_TEST)
  },

  draw2D: function() {
    // 2D çizimler için
  },

  mouseDown: function(x, y, event) {
    return false
  },

  mouseMove: function(x, y, event) {},
  mouseUp: function(x, y, event) {},
  mouseClick: function() { return false },
  mouseDblClick: function() { return false },

  setGeometry: function() {
    this.fillVertexBuffer(this.data)
  },

  free: function() {
    const { gl } = this
    gl.deleteBuffer(this.vertexBuffer)
    gl.deleteProgram(this.shaderProgram)
    this.vertexBuffer = null
    this.shaderProgram = null
  },

  // Yardımcı Metodlar
  createProgramAndFillBuffer: function(data) {
    this.data = data
    this.createShaderProgram()
    this.createVertexBuffer()
    this.fillVertexBuffer(data)
    this.globe.DrawRender()
  },

  setRectangleColor: function(rectColor) {
    // Renk ayarlama
  }
}
```

## Kullanım

```javascript
const data = [
  { long: 32, lat: 40, z: 0 },
  { long: 34, lat: 40, z: 0 },
  { long: 34, lat: 42, z: 0 },
  { long: 32, lat: 42, z: 0 }
]

// Plugin'i kaydet
myGlobe.api_RegisterPlugin(rectanglePlugin)

// Program ve buffer oluştur
rectanglePlugin.createProgramAndFillBuffer(data)
```

## Shader Örneği

```javascript
const vertexShaderCode = `
  precision lowp float;
  attribute vec3 vertexPos;
  uniform mat4 modelViewMatrix;
  uniform mat4 projectionMatrix;
  uniform vec3 transPos;
  uniform vec3 uColor;
  varying vec3 vColor;
  
  void main(void) {
    gl_Position = projectionMatrix * modelViewMatrix * 
                  vec4(vertexPos.xyz - transPos.xyz, 1.0);
    vColor = uColor;
  }
`

const fragmentShaderCode = `
  precision lowp float;
  varying vec3 vColor;
  
  void main() {
    gl_FragColor = vec4(vColor.rgb, 1.0);
  }
`
```
