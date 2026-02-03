var myGlobe; var createObj; var objectbuffer; var objCreator; var CSRasterTypes; var CSMeshTypes; var CSObjectTypes; var CSShapeTypes; var CSIconTypes; var CSOnChangeStates; var CSShowDistanceMode; var CSZMode; var
	CSCorner; var CSOwnerTypes; var CSProcessType; var CSObject3DShapeTypes; var GlobeManager

CSRasterTypes = window.GlobeApi.CSRasterTypes
CSMeshTypes = window.GlobeApi.CSMeshTypes
CSObjectTypes = window.GlobeApi.CSObjectTypes
CSShapeTypes = window.GlobeApi.CSShapeTypes
CSIconTypes = window.GlobeApi.CSIconTypes
CSOnChangeStates = window.GlobeApi.CSOnChangeStates
CSShowDistanceMode = window.GlobeApi.CSShowDistanceMode
CSZMode = window.GlobeApi.CSZMode
CSCorner = window.GlobeApi.CSCorner
CSOwnerTypes = window.GlobeApi.CSOwnerTypes
CSProcessType = window.GlobeApi.CSProcessType
CSObject3DShapeTypes = window.GlobeApi.CSObject3DShapeTypes
CSRasterizeQuality = window.GlobeApi.CSRasterizeQuality
GlobeManager = window.GlobeApi.GlobeManager

const globeManagerParams = {
	startEmptyRaster: false,
	emptyRasterColor: 'rgba(255,255,255,0)',
	rasterize: {
		quality: 512,
		antiAliasingQuality: 1.5
	},
	milIconTexturizeType: window.GlobeApi.CSMilIconTexturizeTypes.LINEAR,
	mesh: {
		url: ['https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL'],
		type: CSMeshTypes.WGS84
	},
	skybox: {
		url: 'http://localhost/symbols/skybox/',
		imageType: window.GlobeApi.CSSkyBoxImageTypes.JPG
	},
	analysisURL: {
		los: 'https://servis.pirireis.com.tr/mesh_service/los',
		visibility: 'https://servis.pirireis.com.tr/mesh_service/viewshed',
		profile: 'https://servis.pirireis.com.tr/mesh_service/los'
	},
	rasterCacheSize: 1000,
	layerCacheSize: 1000,
	globedll: 'https://servis.pirireis.com.tr/webkure/webglobe/webglobeserver.dll'
}

var globeParameters = {
	id: 'globe1',
	canvas: document.getElementById('globe'),
	geometry: window.GlobeApi.CSGeometryTypes.SPHERE,
	globeMaxLodLevel: 22,
	raster: {
		url: 'https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/{z}/{x}/{y}',
		type: CSRasterTypes.XYZ_MERCATOR,
		maxLodLevel: 19,
		opacity: 1.0,
		bbox: null,
		noDatatoEmptyImage: false
	},
	options: {
		showStatusBar: true,
		showCompass: true,
		showOverview: true,
		showScaleBar: true,
		showDebug: true
	}
}

GlobeManager.Initialize(globeManagerParams)
var myGlobe = GlobeManager.Add(globeParameters)


window.myGlobe = myGlobe
// myGlobe.api_SetGeometry(1)
GlobeManager.api_SetMilSymbol(window.ms)
