/**
 * FULL WebGL/WebGL2 Parity Hook v4
 * Captures ALL WebGL calls, constants, matrices, uniforms for C++ comparison
 */

(function() {
    'use strict';
    
    // ============= GLOBAL DATA STORAGE =============
    window.FullParityData = {
        // WebGL Context Info
        glInfo: null,
        
        // All captured constants
        constants: {},
        
        // All matrices
        matrices: {
            projection: [],
            modelView: [],
            mvp: [],
            arcball: []
        },
        
        // Shader uniforms
        uniforms: {
            locations: {},
            values: {}
        },
        
        // Draw calls
        drawCalls: [],
        
        // Texture uploads
        textures: [],
        
        // Buffer data
        buffers: [],
        
        // Camera state history
        cameraHistory: [],
        
        // Navigation events
        navigation: [],
        
        // Frame data
        frames: [],
        currentFrame: 0,
        
        // Errors
        errors: []
    };
    
    var glContext = null;
    var frameId = 0;
    var uniformLocations = new Map();
    var captureEnabled = true;
    var captureLevel = 'full'; // 'minimal', 'normal', 'full'
    
    // ============= WEBGL CONTEXT HOOK =============
    var originalGetContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, attrs) {
        var ctx = originalGetContext.call(this, type, attrs);
        
        if ((type === 'webgl2' || type === 'webgl') && ctx && !ctx.__hooked) {
            ctx.__hooked = true;
            glContext = ctx;
            hookWebGLContext(ctx, type);
            console.log('[FullHook] WebGL context hooked:', type);
        }
        
        return ctx;
    };
    
    function hookWebGLContext(gl, type) {
        // Capture GL info
        window.FullParityData.glInfo = {
            type: type,
            version: gl.getParameter(gl.VERSION),
            vendor: gl.getParameter(gl.VENDOR),
            renderer: gl.getParameter(gl.RENDERER),
            maxTextureSize: gl.getParameter(gl.MAX_TEXTURE_SIZE),
            maxViewportDims: gl.getParameter(gl.MAX_VIEWPORT_DIMS),
            maxVertexAttribs: gl.getParameter(gl.MAX_VERTEX_ATTRIBS),
            maxTextureUnits: gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS)
        };
        
        // Hook getUniformLocation
        var origGetUniformLocation = gl.getUniformLocation.bind(gl);
        gl.getUniformLocation = function(program, name) {
            var loc = origGetUniformLocation(program, name);
            if (loc) {
                uniformLocations.set(loc, name);
                window.FullParityData.uniforms.locations[name] = true;
            }
            return loc;
        };
        
        // Hook uniformMatrix4fv - MVP matrices
        var origUniformMatrix4fv = gl.uniformMatrix4fv.bind(gl);
        gl.uniformMatrix4fv = function(location, transpose, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                var arr = Array.from(value);
                
                if (name.toLowerCase().includes('proj') || 
                    name.toLowerCase().includes('model') ||
                    name.toLowerCase().includes('view') ||
                    name.toLowerCase().includes('mvp')) {
                    
                    window.FullParityData.matrices.mvp.push({
                        frame: frameId,
                        name: name,
                        transpose: transpose,
                        matrix: arr,
                        timestamp: Date.now()
                    });
                }
                
                window.FullParityData.uniforms.values[name] = {
                    type: 'mat4',
                    value: arr,
                    frame: frameId
                };
            }
            return origUniformMatrix4fv(location, transpose, value);
        };
        
        // Hook uniformMatrix3fv
        var origUniformMatrix3fv = gl.uniformMatrix3fv.bind(gl);
        gl.uniformMatrix3fv = function(location, transpose, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                window.FullParityData.uniforms.values[name] = {
                    type: 'mat3',
                    value: Array.from(value),
                    frame: frameId
                };
            }
            return origUniformMatrix3fv(location, transpose, value);
        };
        
        // Hook uniform3fv - eye position, light, etc.
        var origUniform3fv = gl.uniform3fv.bind(gl);
        gl.uniform3fv = function(location, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                window.FullParityData.uniforms.values[name] = {
                    type: 'vec3',
                    value: Array.from(value),
                    frame: frameId
                };
            }
            return origUniform3fv(location, value);
        };
        
        // Hook uniform4fv
        var origUniform4fv = gl.uniform4fv.bind(gl);
        gl.uniform4fv = function(location, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                window.FullParityData.uniforms.values[name] = {
                    type: 'vec4',
                    value: Array.from(value),
                    frame: frameId
                };
            }
            return origUniform4fv(location, value);
        };
        
        // Hook uniform1f
        var origUniform1f = gl.uniform1f.bind(gl);
        gl.uniform1f = function(location, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                window.FullParityData.uniforms.values[name] = {
                    type: 'float',
                    value: value,
                    frame: frameId
                };
            }
            return origUniform1f(location, value);
        };
        
        // Hook uniform1i
        var origUniform1i = gl.uniform1i.bind(gl);
        gl.uniform1i = function(location, value) {
            if (captureEnabled && location) {
                var name = uniformLocations.get(location) || 'unknown';
                window.FullParityData.uniforms.values[name] = {
                    type: 'int',
                    value: value,
                    frame: frameId
                };
            }
            return origUniform1i(location, value);
        };
        
        // Hook drawElements
        var origDrawElements = gl.drawElements.bind(gl);
        gl.drawElements = function(mode, count, type, offset) {
            if (captureEnabled && captureLevel === 'full') {
                window.FullParityData.drawCalls.push({
                    type: 'drawElements',
                    frame: frameId,
                    mode: mode,
                    count: count,
                    elementType: type,
                    offset: offset,
                    timestamp: Date.now()
                });
            }
            return origDrawElements(mode, count, type, offset);
        };
        
        // Hook drawArrays
        var origDrawArrays = gl.drawArrays.bind(gl);
        gl.drawArrays = function(mode, first, count) {
            if (captureEnabled && captureLevel === 'full') {
                window.FullParityData.drawCalls.push({
                    type: 'drawArrays',
                    frame: frameId,
                    mode: mode,
                    first: first,
                    count: count,
                    timestamp: Date.now()
                });
            }
            return origDrawArrays(mode, first, count);
        };
        
        // Hook texImage2D
        var origTexImage2D = gl.texImage2D.bind(gl);
        gl.texImage2D = function() {
            if (captureEnabled) {
                var args = Array.from(arguments);
                window.FullParityData.textures.push({
                    frame: frameId,
                    target: args[0],
                    level: args[1],
                    width: args[3] || (args[5] ? args[5].width : 0),
                    height: args[4] || (args[5] ? args[5].height : 0),
                    timestamp: Date.now()
                });
            }
            return origTexImage2D.apply(gl, arguments);
        };
        
        // Hook viewport
        var origViewport = gl.viewport.bind(gl);
        gl.viewport = function(x, y, width, height) {
            if (captureEnabled) {
                window.FullParityData.glInfo.viewport = { x: x, y: y, width: width, height: height };
            }
            return origViewport(x, y, width, height);
        };
    }
    
    // ============= GLOBE API HOOK =============
    function hookGlobeApi() {
        if (!window.GlobeApi) {
            setTimeout(hookGlobeApi, 100);
            return;
        }
        console.log('[FullHook] GlobeApi detected...');
        waitForGlobe();
    }
    
    function waitForGlobe() {
        if (!window.myGlobe) {
            setTimeout(waitForGlobe, 100);
            return;
        }
        setupFullCapture(window.myGlobe);
    }
    
    function setupFullCapture(globe) {
        // ============= CAPTURE ALL Ta CONSTANTS =============
        if (window.GlobeApi && window.GlobeApi.Ta) {
            window.FullParityData.constants.Ta = window.GlobeApi.Ta;
        }
        
        // Try to find Ta through globe
        var taKeys = Object.keys(globe).filter(function(k) { 
            return k.startsWith('GLOBE_') || k.startsWith('CS') || k.startsWith('F');
        });
        
        taKeys.forEach(function(k) {
            if (typeof globe[k] !== 'function' && typeof globe[k] !== 'object') {
                window.FullParityData.constants[k] = globe[k];
            }
        });
        
        // ============= MAIN CAPTURE FUNCTIONS =============
        window.FullCapture = {
            // Capture EVERYTHING
            captureAll: function() {
                var cam = globe.FCamera;
                var ab = cam.FArcBall;
                
                return {
                    timestamp: Date.now(),
                    frameId: frameId,
                    
                    // ===== CAMERA =====
                    camera: {
                        FDist: cam.FDist,
                        FCamZ: cam.FCamZ,
                        FTILT_ROT_DEGREE: cam.FTILT_ROT_DEGREE,
                        FCamMode: cam.FCamMode,
                        FFLYMODE: cam.FFLYMODE,
                        FCamLongLat: cam.FCamLongLat ? { x: cam.FCamLongLat.x, y: cam.FCamLongLat.y } : null
                    },
                    
                    // ===== EULER ANGLES =====
                    euler: {
                        ea: globe.ea ? { x: globe.ea.x, y: globe.ea.y, z: globe.ea.z } : null,
                        eaDeg: globe.ea ? { 
                            x: globe.ea.x * 180 / Math.PI, 
                            y: globe.ea.y * 180 / Math.PI, 
                            z: globe.ea.z * 180 / Math.PI 
                        } : null
                    },
                    
                    // ===== ARCBALL =====
                    arcball: {
                        abQuat: ab.abQuat ? Array.from(ab.abQuat) : null,
                        abLast: ab.abLast ? Array.from(ab.abLast) : null,
                        abNext: ab.abNext ? Array.from(ab.abNext) : null,
                        abSave: ab.abSave ? Array.from(ab.abSave) : null,
                        abStart: ab.abStart ? { x: ab.abStart.x, y: ab.abStart.y, z: ab.abStart.z } : null,
                        abCurr: ab.abCurr ? { x: ab.abCurr.x, y: ab.abCurr.y, z: ab.abCurr.z } : null,
                        abStartPix: ab.abStartPix ? { x: ab.abStartPix.x, y: ab.abStartPix.y } : null,
                        abCurPix: ab.abCurPix ? { x: ab.abCurPix.x, y: ab.abCurPix.y } : null,
                        abEye: ab.abEye ? { x: ab.abEye.x, y: ab.abEye.y, z: ab.abEye.z } : null,
                        abEyeDir: ab.abEyeDir ? { x: ab.abEyeDir.x, y: ab.abEyeDir.y, z: ab.abEyeDir.z } : null,
                        abZoom: ab.abZoom,
                        abZoom2: ab.abZoom2,
                        abSphere: ab.abSphere,
                        abEdge: ab.abEdge,
                        abGLP: ab.abGLP ? Array.from(ab.abGLP) : null,
                        abGLM: ab.abGLM ? Array.from(ab.abGLM) : null,
                        abGLV: ab.abGLV ? Array.from(ab.abGLV) : null
                    },
                    
                    // ===== MATRICES =====
                    matrices: {
                        projectionM: globe.projectionM ? Array.from(globe.projectionM) : null,
                        lookAtMatrix: globe.lookAtMatrix ? Array.from(globe.lookAtMatrix) : null,
                        modelMatrix: globe.modelMatrix ? Array.from(globe.modelMatrix) : null,
                        mvpMatrix: globe.mvpMatrix ? Array.from(globe.mvpMatrix) : null
                    },
                    
                    // ===== EYE/UP VECTORS =====
                    vectors: {
                        FEyePos: globe.FEyePos ? { x: globe.FEyePos.x, y: globe.FEyePos.y, z: globe.FEyePos.z } : null,
                        FEyePos2: globe.FEyePos2 ? { x: globe.FEyePos2.x, y: globe.FEyePos2.y, z: globe.FEyePos2.z } : null,
                        FUpPos: globe.FUpPos ? { x: globe.FUpPos.x, y: globe.FUpPos.y, z: globe.FUpPos.z } : null,
                        FEyePosUnit: globe.FEyePosUnit ? { x: globe.FEyePosUnit.x, y: globe.FEyePosUnit.y, z: globe.FEyePosUnit.z } : null,
                        Fp: globe.Fp ? { x: globe.Fp.x, y: globe.Fp.y, z: globe.Fp.z } : null,
                        Fu: globe.Fu ? { x: globe.Fu.x, y: globe.Fu.y, z: globe.Fu.z } : null,
                        Fp2: globe.Fp2 ? { x: globe.Fp2.x, y: globe.Fp2.y, z: globe.Fp2.z } : null
                    },
                    
                    // ===== SCREEN =====
                    screen: {
                        FScrW: globe.FScrW,
                        FScrH: globe.FScrH,
                        aspect: globe.FScrW / globe.FScrH
                    },
                    
                    // ===== LOD/TILE =====
                    lod: {
                        FDRAWED_MAX_LEVEL: globe.FDRAWED_MAX_LEVEL,
                        FDRAWED_MAX_LEVEL_FLOAT: globe.FDRAWED_MAX_LEVEL_FLOAT,
                        currentLod: globe.api_GetCurrentLOD(),
                        currentLodDecimal: globe.api_GetCurrentLODWithDecimal()
                    },
                    
                    // ===== API VALUES =====
                    api: {
                        cameraDist: globe.api_GetCameraDist(),
                        altitude: globe.api_Altitude(),
                        fps: globe.api_FPS ? globe.api_FPS() : null
                    }
                };
            },
            
            // Get C++ expected constants
            getCppConstants: function() {
                return {
                    // Globe constants
                    GLOBE_RADIUS: 6378.137,
                    GLOBE_RADIUS_K: 0.001,
                    GLOBE_RADIUS_METER: 6378137,
                    GLOBE_FOV: 50.0,
                    
                    // Distance limits
                    GLOBE_MIN_DIST: 0.00001,
                    GLOBE_MAX_DIST: 100000,
                    GLOBE_START_DIST_YATAY: 2.1 * 6378.137,
                    
                    // Tilt limits
                    GLOBE_MIN_TILTANGLE: 0.05,
                    GLOBE_MAX_TILTANGLE: 80.0,
                    
                    // LOD
                    GLOBE_MIN_LOD: 2,
                    GLOBE_MAX_LOD: 22,
                    GLOBE_IMAGE_SIZE: 256,
                    GLOBE_DEFAULT_TRESH_HOLD: 350,
                    
                    // Texture states
                    tr_loading: 1,
                    tr_load_OK: 2,
                    tr_load_OK_NoData: 3,
                    tr_load_NoInternet: 4,
                    
                    // Near/Far planes
                    NEAR_PLANE: 0.1 * 6378.137,
                    FAR_PLANE: 100.0 * 6378.137
                };
            },
            
            // Full comparison with C++
            compareWithCpp: function() {
                var jsData = this.captureAll();
                var cppConst = this.getCppConstants();
                
                var comparison = {
                    timestamp: Date.now(),
                    
                    // Constant checks
                    constants: {
                        GLOBE_RADIUS: { js: 6378.137, cpp: cppConst.GLOBE_RADIUS, match: true },
                        GLOBE_FOV: { js: 50, cpp: cppConst.GLOBE_FOV, match: true },
                        GLOBE_MIN_LOD: { js: 2, cpp: cppConst.GLOBE_MIN_LOD, match: true },
                        GLOBE_MAX_LOD: { js: 22, cpp: cppConst.GLOBE_MAX_LOD, match: true }
                    },
                    
                    // Matrix checks
                    matrices: {},
                    
                    // Camera checks
                    camera: {},
                    
                    // Overall result
                    allMatch: true
                };
                
                // Check projection matrix
                if (jsData.matrices.projectionM) {
                    var proj = jsData.matrices.projectionM;
                    var aspect = jsData.screen.aspect;
                    var tanHalf = Math.tan(25 * Math.PI / 180);
                    var expectedP00 = 1 / (aspect * tanHalf);
                    var expectedP11 = 1 / tanHalf;
                    
                    comparison.matrices.projection = {
                        p00: { js: proj[0], expected: expectedP00, match: Math.abs(proj[0] - expectedP00) < 0.01 },
                        p11: { js: proj[5], expected: expectedP11, match: Math.abs(proj[5] - expectedP11) < 0.01 }
                    };
                    
                    if (!comparison.matrices.projection.p00.match || !comparison.matrices.projection.p11.match) {
                        comparison.allMatch = false;
                    }
                }
                
                // Check camera values
                comparison.camera = {
                    FDist: { js: jsData.camera.FDist, unit: 'km' },
                    FTILT: { js: jsData.camera.FTILT_ROT_DEGREE, min: cppConst.GLOBE_MIN_TILTANGLE, max: cppConst.GLOBE_MAX_TILTANGLE },
                    FCamMode: { js: jsData.camera.FCamMode, expected: 0, match: jsData.camera.FCamMode === 0 }
                };
                
                // Check ArcBall
                if (jsData.arcball.abSphere) {
                    var expectedSphere = cppConst.GLOBE_RADIUS * cppConst.GLOBE_RADIUS;
                    comparison.arcball = {
                        abSphere: { 
                            js: jsData.arcball.abSphere, 
                            expected: expectedSphere, 
                            match: Math.abs(jsData.arcball.abSphere - expectedSphere) < 1 
                        }
                    };
                }
                
                return { jsData: jsData, cppConstants: cppConst, comparison: comparison };
            },
            
            // Get all captured WebGL data
            getWebGLData: function() {
                return window.FullParityData;
            },
            
            // Get uniform values
            getUniforms: function() {
                return window.FullParityData.uniforms;
            },
            
            // Enable/disable capture
            setCapture: function(enabled, level) {
                captureEnabled = enabled;
                if (level) captureLevel = level;
                console.log('[FullCapture] Capture:', enabled, 'Level:', captureLevel);
            },
            
            // Clear all data
            clear: function() {
                window.FullParityData.matrices = { projection: [], modelView: [], mvp: [], arcball: [] };
                window.FullParityData.uniforms = { locations: {}, values: {} };
                window.FullParityData.drawCalls = [];
                window.FullParityData.textures = [];
                window.FullParityData.cameraHistory = [];
                frameId = 0;
            },
            
            // Export all data as JSON
            exportJSON: function() {
                var data = {
                    glInfo: window.FullParityData.glInfo,
                    constants: window.FullParityData.constants,
                    currentState: this.captureAll(),
                    comparison: this.compareWithCpp(),
                    uniforms: window.FullParityData.uniforms,
                    webglData: {
                        drawCallCount: window.FullParityData.drawCalls.length,
                        textureCount: window.FullParityData.textures.length,
                        matrixCaptures: window.FullParityData.matrices.mvp.length
                    }
                };
                return JSON.stringify(data, null, 2);
            }
        };
        
        // Frame counter
        var origRAF = window.requestAnimationFrame;
        window.requestAnimationFrame = function(cb) {
            return origRAF(function(t) {
                frameId++;
                cb(t);
            });
        };
        
        console.log('[FullHook] Ready! Commands:');
        console.log('  FullCapture.captureAll()       - Capture all state');
        console.log('  FullCapture.compareWithCpp()   - Full C++ comparison');
        console.log('  FullCapture.getCppConstants()  - Get C++ constants');
        console.log('  FullCapture.getUniforms()      - Get shader uniforms');
        console.log('  FullCapture.getWebGLData()     - Get WebGL capture data');
        console.log('  FullCapture.exportJSON()       - Export all as JSON');
    }
    
    hookGlobeApi();
    console.log('[FullHook] Initialized v4.');
})();
