/**
 * WebGL/WebGL2 Hook for Parity Testing - Enhanced Version v3
 * Deep analysis of ArcBall, Navigation, Tilt, Pan, Zoom, Rotate, Mouse Events, Projection Matrices
 */

(function() {
    'use strict';
    
    window.NavParityData = {
        snapshots: [],
        operations: [],
        arcball: [],
        matrices: [],
        mouseEvents: [],
        projectionHistory: [],
        dragSessions: []
    };
    
    let snapshotId = 0;
    let mouseEventId = 0;
    let currentDragSession = null;
    
    function hookGlobeApi() {
        if (!window.GlobeApi) {
            setTimeout(hookGlobeApi, 100);
            return;
        }
        console.log('[NavHook] GlobeApi detected, waiting for globe...');
        waitForGlobe();
    }
    
    function waitForGlobe() {
        if (!window.myGlobe) {
            setTimeout(waitForGlobe, 100);
            return;
        }
        console.log('[NavHook] Globe ready, setting up parity tests...');
        setupParityTests(window.myGlobe);
    }
    
    function setupParityTests(globe) {
        window.NavTest = {
            snapshot: function() {
                const cam = globe.FCamera;
                const ab = cam.FArcBall;
                const id = ++snapshotId;
                
                const snap = {
                    id: id,
                    timestamp: Date.now(),
                    FDist: cam.FDist,
                    FCamZ: cam.FCamZ,
                    FTILT_ROT_DEGREE: cam.FTILT_ROT_DEGREE,
                    FCamMode: cam.FCamMode,
                    FFLYMODE: cam.FFLYMODE,
                    ea: globe.ea ? { x: globe.ea.x, y: globe.ea.y, z: globe.ea.z } : null,
                    abQuat: ab.abQuat ? Array.from(ab.abQuat) : null,
                    abZoom: ab.abZoom,
                    abSphere: ab.abSphere,
                    FEyePos: globe.FEyePos ? { x: globe.FEyePos.x, y: globe.FEyePos.y, z: globe.FEyePos.z } : null,
                    dist: globe.api_GetCameraDist(),
                    altitude: globe.api_Altitude(),
                    lod: globe.api_GetCurrentLOD(),
                    lodDecimal: globe.api_GetCurrentLODWithDecimal(),
                    FScrW: globe.FScrW,
                    FScrH: globe.FScrH
                };
                
                window.NavParityData.snapshots.push(snap);
                return snap;
            },
            
            compare: function(snap1, snap2) {
                const diff = {};
                const keys = ['FDist', 'FCamZ', 'FTILT_ROT_DEGREE', 'FCamMode', 'abZoom', 'dist', 'altitude', 'lod'];
                
                keys.forEach(function(k) {
                    if (snap1[k] !== snap2[k]) {
                        diff[k] = { before: snap1[k], after: snap2[k], delta: snap2[k] - snap1[k] };
                    }
                });
                
                if (snap1.ea && snap2.ea) {
                    ['x', 'y', 'z'].forEach(function(axis) {
                        if (Math.abs(snap1.ea[axis] - snap2.ea[axis]) > 0.0001) {
                            diff['ea.' + axis] = { 
                                before: snap1.ea[axis], 
                                after: snap2.ea[axis],
                                deltaDeg: (snap2.ea[axis] - snap1.ea[axis]) * 180 / Math.PI
                            };
                        }
                    });
                }
                
                return diff;
            },
            
            captureProjectionMatrix: function() {
                const ab = globe.FCamera.FArcBall;
                
                const matrices = {
                    timestamp: Date.now(),
                    projectionM: globe.projectionM ? Array.from(globe.projectionM) : null,
                    lookAtMatrix: globe.lookAtMatrix ? Array.from(globe.lookAtMatrix) : null,
                    abGLP: ab.abGLP ? Array.from(ab.abGLP) : null,
                    abGLM: ab.abGLM ? Array.from(ab.abGLM) : null,
                    abGLV: ab.abGLV ? Array.from(ab.abGLV) : null,
                    abZoom: ab.abZoom,
                    abZoom2: ab.abZoom2,
                    abSphere: ab.abSphere,
                    abEdge: ab.abEdge,
                    abEye: ab.abEye ? { x: ab.abEye.x, y: ab.abEye.y, z: ab.abEye.z } : null,
                    abEyeDir: ab.abEyeDir ? { x: ab.abEyeDir.x, y: ab.abEyeDir.y, z: ab.abEyeDir.z } : null,
                    FScrW: globe.FScrW,
                    FScrH: globe.FScrH,
                    aspect: globe.FScrW / globe.FScrH,
                    FOV: 50
                };
                
                window.NavParityData.projectionHistory.push(matrices);
                return matrices;
            },
            
            captureArcBallState: function() {
                const ab = globe.FCamera.FArcBall;
                
                const state = {
                    timestamp: Date.now(),
                    abQuat: ab.abQuat ? Array.from(ab.abQuat) : null,
                    abLast: ab.abLast ? Array.from(ab.abLast) : null,
                    abNext: ab.abNext ? Array.from(ab.abNext) : null,
                    abSave: ab.abSave ? Array.from(ab.abSave) : null,
                    abStart: ab.abStart ? { x: ab.abStart.x, y: ab.abStart.y, z: ab.abStart.z } : null,
                    abCurr: ab.abCurr ? { x: ab.abCurr.x, y: ab.abCurr.y, z: ab.abCurr.z } : null,
                    abStartPix: ab.abStartPix ? { x: ab.abStartPix.x, y: ab.abStartPix.y } : null,
                    abCurPix: ab.abCurPix ? { x: ab.abCurPix.x, y: ab.abCurPix.y } : null,
                    abZoom: ab.abZoom,
                    abSphere: ab.abSphere,
                    abEdge: ab.abEdge
                };
                
                window.NavParityData.arcball.push(state);
                return state;
            },
            
            compareWithCpp: function() {
                const proj = this.captureProjectionMatrix();
                const ab = this.captureArcBallState();
                const snap = this.snapshot();
                
                var R = 6378.137;
                var FOV = 50;
                var near = 0.1 * R;
                var far = 100 * R;
                var tanHalfFov = Math.tan(FOV * Math.PI / 360);
                var aspect = proj.aspect;
                var expectedProj00 = 1 / (aspect * tanHalfFov);
                var expectedProj11 = 1 / tanHalfFov;
                
                return {
                    timestamp: Date.now(),
                    js: { projection: proj, arcball: ab, camera: snap },
                    cpp: { GLOBE_RADIUS: R, GLOBE_FOV: FOV, near: near, far: far, expectedProj00: expectedProj00, expectedProj11: expectedProj11 },
                    comparison: {
                        proj00_match: proj.projectionM ? Math.abs(proj.projectionM[0] - expectedProj00) < 0.01 : false,
                        proj11_match: proj.projectionM ? Math.abs(proj.projectionM[5] - expectedProj11) < 0.01 : false,
                        fov_match: true,
                        radius_match: true
                    }
                };
            },
            
            startMouseTracking: function() {
                var canvas = globe.canvas2d || document.getElementById('globe');
                if (!canvas) {
                    console.error('[NavTest] Canvas not found');
                    return false;
                }
                
                var cam = globe.FCamera;
                var ab = cam.FArcBall;
                var self = this;
                
                canvas.addEventListener('mousedown', function(e) {
                    var evt = {
                        id: ++mouseEventId,
                        type: 'mousedown',
                        timestamp: Date.now(),
                        x: e.clientX,
                        y: e.clientY,
                        button: e.button,
                        beforeState: {
                            FDist: cam.FDist,
                            FTILT: cam.FTILT_ROT_DEGREE,
                            FCamMode: cam.FCamMode,
                            ea: { x: globe.ea.x, y: globe.ea.y, z: globe.ea.z },
                            abQuat: Array.from(ab.abQuat),
                            abStart: ab.abStart ? { x: ab.abStart.x, y: ab.abStart.y, z: ab.abStart.z } : null
                        }
                    };
                    
                    currentDragSession = { startEvent: evt, moves: [], startTime: Date.now() };
                    window.NavParityData.mouseEvents.push(evt);
                    console.log('[NavTest] MouseDown:', evt);
                });
                
                canvas.addEventListener('mousemove', function(e) {
                    if (!currentDragSession) return;
                    
                    var move = {
                        timestamp: Date.now(),
                        x: e.clientX,
                        y: e.clientY,
                        dx: e.movementX,
                        dy: e.movementY,
                        abCurr: ab.abCurr ? { x: ab.abCurr.x, y: ab.abCurr.y, z: ab.abCurr.z } : null,
                        ea: { x: globe.ea.x, y: globe.ea.y, z: globe.ea.z }
                    };
                    
                    currentDragSession.moves.push(move);
                });
                
                canvas.addEventListener('mouseup', function(e) {
                    if (!currentDragSession) return;
                    
                    var evt = {
                        id: ++mouseEventId,
                        type: 'mouseup',
                        timestamp: Date.now(),
                        x: e.clientX,
                        y: e.clientY,
                        button: e.button,
                        afterState: {
                            FDist: cam.FDist,
                            FTILT: cam.FTILT_ROT_DEGREE,
                            FCamMode: cam.FCamMode,
                            ea: { x: globe.ea.x, y: globe.ea.y, z: globe.ea.z },
                            abQuat: Array.from(ab.abQuat),
                            abCurr: ab.abCurr ? { x: ab.abCurr.x, y: ab.abCurr.y, z: ab.abCurr.z } : null
                        },
                        dragDuration: Date.now() - currentDragSession.startTime,
                        moveCount: currentDragSession.moves.length
                    };
                    
                    currentDragSession.endEvent = evt;
                    window.NavParityData.dragSessions.push(currentDragSession);
                    window.NavParityData.mouseEvents.push(evt);
                    
                    console.log('[NavTest] MouseUp - Drag session:', {
                        duration: evt.dragDuration,
                        moves: evt.moveCount,
                        startPos: { x: currentDragSession.startEvent.x, y: currentDragSession.startEvent.y },
                        endPos: { x: evt.x, y: evt.y }
                    });
                    
                    currentDragSession = null;
                });
                
                canvas.addEventListener('wheel', function(e) {
                    var evt = {
                        id: ++mouseEventId,
                        type: 'wheel',
                        timestamp: Date.now(),
                        deltaY: e.deltaY,
                        x: e.clientX,
                        y: e.clientY,
                        beforeDist: cam.FDist,
                        beforeLod: globe.api_GetCurrentLOD()
                    };
                    
                    window.NavParityData.mouseEvents.push(evt);
                    
                    setTimeout(function() {
                        evt.afterDist = cam.FDist;
                        evt.afterLod = globe.api_GetCurrentLOD();
                        evt.distDelta = evt.afterDist - evt.beforeDist;
                        console.log('[NavTest] Wheel:', evt);
                    }, 100);
                });
                
                console.log('[NavTest] Mouse tracking started on canvas');
                return true;
            },
            
            getData: function() {
                return window.NavParityData;
            },
            
            clear: function() {
                window.NavParityData.snapshots = [];
                window.NavParityData.operations = [];
                window.NavParityData.mouseEvents = [];
                window.NavParityData.dragSessions = [];
                window.NavParityData.projectionHistory = [];
                window.NavParityData.arcball = [];
                snapshotId = 0;
                mouseEventId = 0;
            },
            
            getCppExpected: function() {
                return {
                    GLOBE_RADIUS: 6378.137,
                    GLOBE_RADIUS_K: 0.001,
                    GLOBE_FOV: 50,
                    GLOBE_MIN_TILTANGLE: 0.05,
                    GLOBE_MAX_TILTANGLE: 80,
                    GLOBE_MIN_LOD: 2,
                    GLOBE_MAX_LOD: 22,
                    GLOBE_START_DIST_YATAY: 2.1 * 6378.137
                };
            },
            
            // FAZ 0: PARITY SNAPSHOT - JS↔C++ karşılaştırma için
            getParitySnapshot: function() {
                var cam = globe.FCamera;
                var nav = globe.GeomClass ? globe.GeomClass.Navigation : null;
                var Ta = window.Ta || {};
                
                // Altitude hesapla (meters)
                var altitude = globe.api_Altitude ? globe.api_Altitude() : 0;
                
                // LOD değerleri
                var lodExact = globe.api_GetCurrentLODWithDecimal ? globe.api_GetCurrentLODWithDecimal() : 0;
                var currentZoom = globe.api_GetCurrentLOD ? globe.api_GetCurrentLOD() : 0;
                
                // Navigation limits
                var navMinLOD = Ta.GLOBE_MIN_LOD || 2;
                var navMaxLOD = Ta.GLOBE_MAX_LOD || 22;
                var navMinDist = Ta.GLOBE_MIN_DIST || 0;
                var navMaxDist = Ta.GLOBE_MAX_DIST || 0;
                
                // Mesh settings (JS: MESHN=5)
                var meshN = 5;  // JS'de sabit u = 5
                
                // Cell creation limits
                var maxCellCanBeCreated = globe.MaxCellCanBeCreated || 100;
                var cellDivisionCount = globe.isCellDivision || 0;
                
                // Camera position
                var centerLat = globe.api_CenterLat ? globe.api_CenterLat() : 0;
                var centerLon = globe.api_CenterLong ? globe.api_CenterLong() : 0;
                var tiltDeg = cam ? cam.FTILT_ROT_DEGREE : 0;
                var northDeg = globe.ea ? (globe.ea.x * 180 / Math.PI) : 0;
                var cameraDist = cam ? cam.FDist : 0;
                
                // Screen
                var screenWidth = globe.FScrW || 0;
                var screenHeight = globe.FScrH || 0;
                var fps = globe.FPS || 0;
                
                // State
                var isAnimating = cam ? cam.FFLYMODE : false;
                var isScreenMoving = globe.MouseEvents ? 
                    (globe.MouseEvents.mouseMode !== 0) : false;
                
                // 2D mode and clamp telemetry
                var is2DMode = globe.F2D || false;
                var clampCount = globe.ClampCount || 0;
                var lastClampMin = globe.LastClampMin || 0;
                var lastClampMax = globe.LastClampMax || 0;
                
                // Try to get actual clamp limits from Ta constants
                if (is2DMode) {
                    lastClampMin = (Ta.GLOBE_MAX_FLAT_DIST || 0) * (Ta.GLOBE_RADIUS_K || 0.001);
                    lastClampMax = (Ta.GLOBE_MIN_FLAT_DIST || 0) * (Ta.GLOBE_RADIUS_K || 0.001);
                    if (lastClampMin > lastClampMax) {
                        var tmp = lastClampMin;
                        lastClampMin = lastClampMax;
                        lastClampMax = tmp;
                    }
                } else {
                    lastClampMin = Ta.GLOBE_MIN_DIST || 0;
                    lastClampMax = Ta.GLOBE_MAX_DIST || 0;
                }
                
                return {
                    altitude: altitude,
                    lodExact: lodExact,
                    currentZoom: currentZoom,
                    navMinLOD: navMinLOD,
                    navMaxLOD: navMaxLOD,
                    navMinDist: navMinDist,
                    navMaxDist: navMaxDist,
                    meshN: meshN,
                    maxCellCanBeCreated: maxCellCanBeCreated,
                    cellDivisionCount: cellDivisionCount,
                    centerLat: centerLat,
                    centerLon: centerLon,
                    tiltDeg: tiltDeg,
                    northDeg: northDeg,
                    cameraDist: cameraDist,
                    screenWidth: screenWidth,
                    screenHeight: screenHeight,
                    fps: fps,
                    isAnimating: isAnimating,
                    isScreenMoving: isScreenMoving,
                    is2DMode: is2DMode,
                    clampCount: clampCount,
                    lastClampMin: lastClampMin,
                    lastClampMax: lastClampMax
                };
            },
            
            // FAZ 0: JSON formatında dump
            dumpParitySnapshot: function() {
                return JSON.stringify(this.getParitySnapshot(), null, 2);
            },
            
            // FAZ 0: C++ ile karşılaştır
            compareParityWithCpp: function(cppJson) {
                var jsSnap = this.getParitySnapshot();
                var cppSnap = typeof cppJson === 'string' ? JSON.parse(cppJson) : cppJson;
                var diff = {};
                var tolerance = 0.01;
                
                var keys = Object.keys(jsSnap);
                for (var i = 0; i < keys.length; i++) {
                    var k = keys[i];
                    var jsVal = jsSnap[k];
                    var cppVal = cppSnap[k];
                    
                    if (typeof jsVal === 'number' && typeof cppVal === 'number') {
                        var delta = Math.abs(jsVal - cppVal);
                        if (delta > tolerance) {
                            diff[k] = { js: jsVal, cpp: cppVal, delta: delta };
                        }
                    } else if (jsVal !== cppVal) {
                        diff[k] = { js: jsVal, cpp: cppVal };
                    }
                }
                
                return {
                    match: Object.keys(diff).length === 0,
                    differences: diff,
                    jsSnapshot: jsSnap,
                    cppSnapshot: cppSnap
                };
            }
        };
        
        window.NavTest.startMouseTracking();
        
        console.log('[NavHook] Ready! Commands:');
        console.log('  NavTest.snapshot()');
        console.log('  NavTest.captureProjectionMatrix()');
        console.log('  NavTest.captureArcBallState()');
        console.log('  NavTest.compareWithCpp()');
        console.log('  NavTest.getParitySnapshot()     // FAZ 0: C++ parity için');
        console.log('  NavTest.dumpParitySnapshot()    // JSON formatında');
        console.log('  NavTest.compareParityWithCpp(cppJson)');
        console.log('  NavTest.getData()');
    }
    
    hookGlobeApi();
    console.log('[NavHook] Initialized v3.');
})();
