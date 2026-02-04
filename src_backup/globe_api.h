#pragma once

#include "globe_api_generated.h"
#include "globe_engine.h"
#include "layer_manager.h"

#include <unordered_map>
#include <vector>

class GlobeApi : public GlobeApiGenerated {
public:
  GlobeApi() = default;
  ~GlobeApi() override = default;

  bool Init(const GlobeConfig& config);
  void Run();
  bool RunLodTest();
  bool RunDemTest();
  bool Run2DClampTest();
  bool RunParityTest();
  
  // ==============================================================================================
  void Shutdown();

  Value api_GlobeIsValid() override;
  Value api_GetCurrentLOD() override;
  Value api_GetCurrentLODWithDecimal() override;
  Value api_SetNavigationDist(const Value& a0) override;
  Value api_SetNavigationLOD(const Value& a0) override;
  Value api_SetMinNavigationLOD(const Value& a0) override;
  Value api_SetMaxNavigationLOD(const Value& a0) override;
  Value api_SetScreenWidth(const Value& a0, const Value& a1) override;
  Value api_CancelScreenWidthAndMinMaxLOD() override;
  Value api_SetMeshCacheSize(const Value& a0) override;
  Value api_ReTryAtMeshTimeout(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_SetTiltAngle(const Value& a0) override;
  Value api_SetNorthAngle(const Value& a0) override;
  Value api_Set2DMode(const Value& a0) override;
  Value api_ZoomToLOD(const Value& a0) override;
  Value api_ZoomToPaperScale(const Value& a0) override;
  Value api_FlyToPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;
  Value api_FlyToPointDirect(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;
  Value api_FlyToRegion(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;
  Value api_FlyToRegionDirect(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;

  // Getter APIs
  Value api_GetCameraDist() override;
  Value api_ScrW() override;
  Value api_ScrH() override;
  Value api_FPS() override;
  Value api_GetTextureCacheSize(); // Added for memory metrics
  Value api_Altitude() override;
  Value api_CamZ() override;
  Value api_OrbitDistance() override;
  Value api_NorthAngleDeg() override;
  Value api_GetScreenCenterAsDegree() override;
  Value api_GetCurrentLookInfo() override;
  Value api_IsScreenMoving() override;
  Value api_GetCurrentScale() override;
  Value api_GetCurrentMinLOD() override;
  Value api_GetCurrentWorldLimit() override;
  Value api_GetCurrentWorldWH() override;
  Value api_GetDirectPosNatural() override;
  Value api_GlobeFree() override;

  // Camera APIs
  Value api_SetCameraPos(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5) override;
  Value api_LeaveCamera(const Value& a0) override;
  Value api_SetDirectPosNatural(const Value& a0) override;
  Value api_SetLockNorth(const Value& a0) override;
  Value api_SetContinuousRotation(const Value& a0) override;

  // Navigation limits
  Value api_SetMinNavigationDist(const Value& a0) override;
  Value api_SetMaxNavigationDist(const Value& a0) override;
  Value api_GetNavigationSpeed() override;
  Value api_SetNavigationSpeed(const Value& a0) override;
  Value api_SetMouseWheelMode(const Value& a0) override;
  Value api_SetMouseWheelDirection(const Value& a0) override;
  Value api_SetLang(const Value& a0) override;
  Value api_SetArrowKeysNavSpeed(const Value& a0) override;

  // Layer APIs
  Value api_AddLayer(const Value& a0, const Value& a1) override;
  Value api_AddObjectBuffer(const Value& a0) override;
  Value api_CreateObjectBuffer(const Value& a0, const Value& a1) override;
  Value api_DeleteObjectBufferById(const Value& a0) override;
  Value api_DeleteAllObjectBuffers() override;
  Value api_DeleteObjectBufferByIndex(const Value& a0) override;
  Value api_FindObjectBufferById(const Value& a0) override;
  Value api_GetObjectBuffer(const Value& a0) override;
  Value api_ObjectBufferCount() override;
  Value api_DeleteLayer(const Value& a0) override;
  Value api_DeleteLayers() override;
  Value api_GetLayer(const Value& a0) override;
  Value api_GetLayerById(const Value& a0) override;
  Value api_LayerCount() override;
  Value api_GetNewLayerId() override;
  Value api_GetNewObjectBufferId() override;
  Value api_GetTotalLayersAsJSON() override;
  Value api_GetZClient(const Value& a0, const Value& a1) override;
  Value api_SetLayerOn(const Value& a0, const Value& a1) override;
  Value api_GetLayerOn(const Value& a0) override;
  Value api_SetLayerOpacity(const Value& a0, const Value& a1) override;
  Value api_GetLayerStyle(const Value& a0) override;
  Value api_SetLayerStyle(const Value& a0, const Value& a1);
  Value api_LayerStyleChanged(const Value& a0) override;
  Value api_QueryByScreen(const Value& a0, const Value& a1) override;
  Value api_QueryByBBox(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_GetGeoFromScreenPoint(const Value& a0, const Value& a1) override;
  Value api_GetScreenPointFromGeo(const Value& a0, const Value& a1) override;
  Value api_CanPickPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5) override;
  Value api_GetDefaultStyle() override;
  Value api_GetDefaultLayerStyle() override;
  Value api_GetDefaultClusterStyle(const Value& a0) override;
  Value api_GetDefaultCompositeLayerStyle() override;
  Value api_GeoJSONToObjectArrData(const Value& a0) override;
  Value api_ObjectCreator(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_SetFlashPeriod(const Value& a0) override;
  Value api_GetFlashPeriod() override;
  Value api_AddCustomFont(const Value& a0, const Value& a1) override;
  Value api_AddIconMap(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  
  // Image Overlay
  Value api_AddImageOverlay(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) override;
  Value api_SetImageOverlayColor(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_ChangeImageOverlayURL(const Value& a0, const Value& a1) override;
  Value api_DeleteImageOverlay(const Value& a0, const Value& a1) override;
  
  // Analysis
  Value api_LineOfSight(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) override;
  Value api_FindProfile(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;

  Value api_SetMouseEvents(const Value& a0, const Value& a1) override;
  Value api_GetMouseEvent(const Value& a0) override;
  Value api_ClearMouseEvents() override;
  Value api_SetZoomWheelInDist(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;
  Value api_SetZoomWheelOutDist(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) override;
  Value api_SetCameraCallBack(const Value& a0) override;
  Value api_SetStatusBarCallBack(const Value& a0) override;
  Value api_SetUndoBuffersChangedEvent(const Value& a0) override;
  Value api_EditCallbackChanged() override;
  Value api_EditCallbackCreator() override;
  Value api_GetEditCallback() override;
  Value api_DispatchEvent(const Value& a0, const Value& a1) override;
  Value api_TriggerCallback(const Value& a0, const Value& a1) override;

  // Conversion / Mercator APIs (Phase 2)
  Value api_GetMercatorPoint(const Value& a0, const Value& a1) override;
  Value api_GetMercator2DPoint(const Value& a0, const Value& a1) override;
  Value api_GetMercator3DPoint(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_GetMercator3DPoints(const Value& a0) override;
  Value api_GetMercator3DPointsByGeoArr(const Value& a0, const Value& a1) override;
  Value api_GetMercator3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1) override;
  Value api_GeoToDMS(const Value& a0, const Value& a1) override;
  Value api_DMSToGeo(const Value& a0) override;
  Value api_GetUTMZone(const Value& a0, const Value& a1) override;
  Value api_GeoToUTM(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_UTMToGeo(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_GeoToMGRS(const Value& a0) override;
  Value api_MGRSToGeo(const Value& a0) override;
  Value api_GeoToGeoRef(const Value& a0, const Value& a1) override;
  Value api_GeoRefToGeo(const Value& a0) override;

  // 3D point APIs (Phase 2)
  Value api_Get3DPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_Get3DPoints(const Value& a0, const Value& a1) override;
  Value api_Get3DPointsByGeoArr(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Get3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_GetCartesian3DPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_GetCartesian3DPoints(const Value& a0, const Value& a1) override;
  Value api_GetCartesian3DPointsByGeoArr(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_GetCartesian3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1, const Value& a2) override;

  // Draw APIs (Phase 2)
  Value api_Draw3dDashedLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) override;
  Value api_Draw3dDashedLineLoop(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) override;
  Value api_Draw3dDashedLineLoopCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Draw3dDashedLineStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) override;
  Value api_Draw3dDashedLineStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Draw3dLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) override;
  Value api_Draw3dLineCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Draw3dLineLoop(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) override;
  Value api_Draw3dLineLoopCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Draw3dLineStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) override;
  Value api_Draw3dLineStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_Draw3dPolygon(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9) override;
  Value api_Draw3dPolygonCurModelProjection(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_Draw3dPolygonStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9) override;
  Value api_Draw3dPolygonStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_DrawBaseGlobeColor(const Value& a0) override;
  Value api_DrawCircle(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) override;
  Value api_DrawCircleCurModelProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_DrawContextText(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) override;
  Value api_DrawContextTextInScreen(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_DrawContextTextMultiLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) override;
  Value api_DrawIcon(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6) override;
  Value api_DrawIconCurProjection(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_GetDrawCommands() override;
  Value api_ClearDrawCommands() override;

  // P0 blockers
  Value api_GlobeVersion() override;
  Value api_GetCurrentGeometry() override;
  Value api_SetGeometry(const Value& a0) override;
  Value api_GetMousePos() override;
  Value api_GetMouseDeg() override;
  Value api_GetGL() override;

  // Raster support (P1)
  Value api_AddRaster(const Value& a0, const Value& a1) override;
  Value api_SetRasterService(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_DeleteRaster(const Value& a0) override;
  Value api_GetRaster(const Value& a0) override;
  Value api_GetRasterById(const Value& a0) override;
  Value api_SetRasterONOFF(const Value& a0, const Value& a1) override;
  Value api_GetRasterONOFF(const Value& a0) override;
  Value api_GetRasterONOFFById(const Value& a0) override;
  Value api_SetRasterOpacity(const Value& a0, const Value& a1) override;
  Value api_GetRasterOpacity(const Value& a0) override;
  Value api_SetRasterZIndex(const Value& a0, const Value& a1) override;
  Value api_GetRasterZIndex(const Value& a0) override;
  Value api_GetNewRasterId() override;
  Value api_RasterCount() override;

  // WMS Overlay APIs (Phase 10+)
  Value api_AddWMSOverlay(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6) override;
  Value api_SetWMSOverlayColor(const Value& a0, const Value& a1, const Value& a2) override;
  Value api_DeleteWMSOverlay(const Value& a0) override;

  // Edit APIs (Phase 3)
  Value api_StartEditObj(const Value& a0, const Value& a1, const Value& a2, const Value& a3) override;
  Value api_StopEditObj(const Value& a0, const Value& a1) override;
  Value api_CanUndoEdit() override;
  Value api_CanRedoEdit() override;
  Value api_UndoEdit() override;
  Value api_RedoEdit() override;
  
  // Plugin APIs (Phase 3)
  Value api_RegisterPlugin(const Value& a0, const Value& a1) override;
  Value api_UnRegisterPlugin(const Value& a0) override;
  Value api_GetPlugin(const Value& a0) override;
  Value api_GetAllPluginsId() override;
  
  // Navigation history APIs (Phase 4C)
  Value api_GoToPreviousPosition() override;
  Value api_GoToNextPosition() override;
  Value api_IsPreviousPositionAvailable() override;
  Value api_IsNextPositionAvailable() override;
  Value api_TurnToNorth(const Value& a0) override;
  Value api_TurnToNorthDirect(const Value& a0) override;

private:
  struct DrawCommand {
    std::string name;
    std::vector<Value> args;
  };

  void RecordDrawCommand(const std::string& name, std::vector<Value> args);

  GlobeEngine engine_;
  bool valid_ = false;
  std::string currentGeometry_ = "Sphere";
  int mouseX_ = 0;
  int mouseY_ = 0;
  int nextRasterId_ = 1;
  int nextObjectBufferId_ = 1;
  std::vector<std::function<void()>> commandQueue_;
  std::mutex queueMutex_;
  
  std::unordered_map<std::string, Value> mouseEvents_;
  std::unordered_map<std::string, Value> callbacks_;
  std::vector<DrawCommand> drawCommands_;
  
  // Edit state (Phase 3)
  struct EditState {
    bool active = false;
    std::string objectId;
    Value objectData;
    std::vector<Value> undoStack;
    std::vector<Value> redoStack;
  };
  EditState editState_;
  
  // Plugin registry (Phase 3)
  std::unordered_map<std::string, Value> plugins_;
};
