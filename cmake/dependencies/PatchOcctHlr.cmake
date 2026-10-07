cmake_minimum_required(VERSION 3.24)
# Reproducible AgentCAD modification of checksum-pinned OCCT 8.0.1.
# Original OCCT copyright/license notices remain in every modified file.
# Patch context is from OCCT, Copyright (c) 1992-1999 Matra Datavision and
# Copyright (c) 1999-2014 OPEN CASCADE SAS, under LGPL 2.1 with the
# Open CASCADE exception (the pinned source archive contains the exact texts).
if(NOT DEFINED OCCT_SOURCE_DIR)
  message(FATAL_ERROR "OCCT_SOURCE_DIR is required")
endif()
function(agentcad_read_occt name upstream_sha256 patched_sha256)
  set(source "${OCCT_SOURCE_DIR}/src/ModelingAlgorithms/TKHLR/HLRBRep/${name}")
  file(READ "${source}" original)
  string(REPLACE "\r\n" "\n" original "${original}")
  string(SHA256 digest "${original}")
  if(NOT digest STREQUAL upstream_sha256 AND NOT digest STREQUAL patched_sha256
      AND NOT digest IN_LIST ARGN)
    message(FATAL_ERROR "OCCT HLR patch requires pinned 8.0.1 ${name}; got ${digest}")
  endif()
  set(source "${source}" PARENT_SCOPE)
  set(modified "${original}" PARENT_SCOPE)
  set(already_patched FALSE PARENT_SCOPE)
  if(digest STREQUAL patched_sha256 OR digest IN_LIST ARGN)
    set(already_patched TRUE PARENT_SCOPE)
  endif()
endfunction()

agentcad_read_occt(HLRBRep_Intersector.cxx
  d5c4b54eb27d6a4ed9d43a7438100df1ac5a420d6d25040ea1577af41f94bc4f
  62887180787c819e6d9597c1ea15a8b84741132573cd5547f71853485152eb83
  41258b6390dc96374102ef57e7add7816b6141bd35d9cd4e20d94451b3795159)
if(NOT already_patched)
  set(before [==[  #define No_Exception
#endif

#include <Bnd_Box.hxx>
#include <ElCLib.hxx>
#include <gp_Lin.hxx>
#include <HLRBRep_CurveTool.hxx>
]==])
  set(after [==[  #define No_Exception
#endif

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#include <Bnd_Box.hxx>
#include <algorithm>
#include <ElCLib.hxx>
#include <gp_Lin.hxx>
#include <HLRBRep_CurveTool.hxx>
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[
void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P)
{
  myTypePerform                 = 2;
  const GeomAbs_SurfaceType typ = HLRBRep_SurfaceTool::GetType(mySurface);
  switch (typ)
]==])
  set(after [==[
void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P)
{
  Perform(L, P, false);
}

void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P, const bool theDense)
{
  myTypePerform                 = 2;
  const GeomAbs_SurfaceType typ = HLRBRep_SurfaceTool::GetType(mySurface);
  switch (typ)
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[      myCSIntersector.Perform(L, mySurface);
      break;
    default: {
      if (myPolyhedron == nullptr)
      {
        int    nbsu, nbsv;
        double u1, v1, u2, v2;
        u1           = HLRBRep_SurfaceTool::FirstUParameter(mySurface);
        v1           = HLRBRep_SurfaceTool::FirstVParameter(mySurface);
        u2           = HLRBRep_SurfaceTool::LastUParameter(mySurface);
        v2           = HLRBRep_SurfaceTool::LastVParameter(mySurface);
        nbsu         = HLRBRep_SurfaceTool::NbSamplesU(mySurface, u1, u2);
        nbsv         = HLRBRep_SurfaceTool::NbSamplesV(mySurface, v1, v2);
        myPolyhedron = new HLRBRep_ThePolyhedronOfInterCSurf(mySurface, nbsu, nbsv, u1, v1, u2, v2);
      }
      double x0, y0, z0, x1, y1, z1, pmin, pmax; //,pp;
      myPolyhedron->Bounding().Get(x0, y0, z0, x1, y1, z1);
      //-- On va rejeter tous les points de parametres > P
]==])
  set(after [==[      myCSIntersector.Perform(L, mySurface);
      break;
    default: {
      const double u1 = HLRBRep_SurfaceTool::FirstUParameter(mySurface);
      const double v1 = HLRBRep_SurfaceTool::FirstVParameter(mySurface);
      const double u2 = HLRBRep_SurfaceTool::LastUParameter(mySurface);
      const double v2 = HLRBRep_SurfaceTool::LastVParameter(mySurface);
      int nbsu = HLRBRep_SurfaceTool::NbSamplesU(mySurface, u1, u2);
      int nbsv = HLRBRep_SurfaceTool::NbSamplesV(mySurface, v1, v2);
      const auto& support = mySurface->Surface();
      if (theDense && support.GetType() == GeomAbs_BSplineSurface)
      {
        nbsu = std::clamp(support.NbUKnots() * support.UDegree(), nbsu, 512);
        nbsv = std::clamp(support.NbVKnots() * support.VDegree(), nbsv, 512);
      }
      if (myPolyhedron != nullptr)
      {
        int oldU, oldV;
        myPolyhedron->Size(oldU, oldV);
        if (oldU != std::max(3, nbsu) || oldV != std::max(3, nbsv))
        {
          delete myPolyhedron;
          myPolyhedron = nullptr;
        }
      }
      if (myPolyhedron == nullptr)
        myPolyhedron = new HLRBRep_ThePolyhedronOfInterCSurf(mySurface, nbsu, nbsv, u1, v1, u2, v2);
      double x0, y0, z0, x1, y1, z1, pmin, pmax; //,pp;
      myPolyhedron->Bounding().Get(x0, y0, z0, x1, y1, z1);
      //-- On va rejeter tous les points de parametres > P
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "62887180787c819e6d9597c1ea15a8b84741132573cd5547f71853485152eb83")
    message(FATAL_ERROR "OCCT HLR patch output mismatch for HLRBRep_Intersector.cxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

agentcad_read_occt(HLRBRep_Intersector.hxx
  169b4ba95914e0e01449f5727a7c9087341ba8b4e0cc2f10497c60cea6dbe022
  2d83d4a6002d9a974f2f9131ba7d44baccbb2e3874b74ee4d155d3f36948ca7e
  21487af005a49a84cfb300f134eb441a8d9261fa9a632cc828090ffa299973b9)
if(NOT already_patched)
  set(before [==[//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

#ifndef _HLRBRep_Intersector_HeaderFile
#define _HLRBRep_Intersector_HeaderFile
]==])
  set(after [==[//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#ifndef _HLRBRep_Intersector_HeaderFile
#define _HLRBRep_Intersector_HeaderFile
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[
  Standard_EXPORT void Perform(const gp_Lin& theL, const double theP);

  Standard_EXPORT bool IsDone() const;

  Standard_EXPORT int NbPoints() const;
]==])
  set(after [==[
  Standard_EXPORT void Perform(const gp_Lin& theL, const double theP);

  Standard_EXPORT void Perform(const gp_Lin& theL, const double theP, const bool theDense);

  Standard_EXPORT bool IsDone() const;

  Standard_EXPORT int NbPoints() const;
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "2d83d4a6002d9a974f2f9131ba7d44baccbb2e3874b74ee4d155d3f36948ca7e")
    message(FATAL_ERROR "OCCT HLR patch output mismatch for HLRBRep_Intersector.hxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

agentcad_read_occt(HLRBRep_Data.cxx
  c9641561b4f0ba15c76d0f50f4d45e03a9dc7272d9260f441abe60ba5625d8b3
  6c781e442f448751e32214db6b6e52a8de0976e207c157261d9d9bc448bec750
  397f666cd0bf1f7c1ce5ef6df7317a3edff9682190dd4d0396e8f1585ab447ca)
if(NOT already_patched)
  set(before [==[// commercial license or contractual agreement.

// #define No_Standard_OutOfRange

#include <BRepTopAdaptor_Tool.hxx>
#include <BRepTopAdaptor_TopolTool.hxx>
]==])
  set(after [==[// commercial license or contractual agreement.

// #define No_Standard_OutOfRange

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#include <BRepTopAdaptor_Tool.hxx>
#include <BRepTopAdaptor_TopolTool.hxx>
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[                                    int&                    Level,
                                    const double            param)
{
  (void)E; // avoid compiler warning

  nbClassification++;
]==])
  set(after [==[                                    int&                    Level,
                                    const double            param)
{
  return ClassifyImpl(E, ED, LevelFlag, Level, param, true);
}

TopAbs_State HLRBRep_Data::ClassifyAtPoint(const int E, const HLRBRep_EdgeData& ED, const double param)
{
  int level = 0;
  return ClassifyImpl(E, ED, true, level, param, false);
}

TopAbs_State HLRBRep_Data::ClassifyImpl(const int E, const HLRBRep_EdgeData& ED,
                                       const bool LevelFlag, int& Level, const double param,
                                       const bool CountLevels)
{
  (void)E; // avoid compiler warning

  nbClassification++;
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[  }

  gp_Lin L    = myProj.Shoot(Psta.X(), Psta.Y());
  double wLim = ElCLib::Parameter(L, PLim);
  myIntersector.Perform(L, wLim);
  if (myIntersector.IsDone())
  {
    int nbPoints = myIntersector.NbPoints();
]==])
  set(after [==[  }

  gp_Lin L    = myProj.Shoot(Psta.X(), Psta.Y());
  const double rayLimit = ElCLib::Parameter(L, PLim);
  const bool denseFirst = !CountLevels && iFaceGeom->Surface().GetType() == GeomAbs_BSplineSurface;
  for (int attempt = 0; attempt < (denseFirst ? 2 : 1); ++attempt)
  {
  double wLim = rayLimit;
  myIntersector.Perform(L, wLim, denseFirst && attempt == 0);
  if (myIntersector.IsDone())
  {
    int nbPoints = myIntersector.NbPoints();
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[          {
            state = TopAbs_IN;
            Level++;
            if (!LevelFlag)
            {
              return state;
            }
]==])
  set(after [==[          {
            state = TopAbs_IN;
            Level++;
            if (!LevelFlag || !CountLevels)
            {
              return state;
            }
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[        }
      }
    }
  }
  return state;
}
]==])
  set(after [==[        }
      }
    }
  }
  }
  return state;
}
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "6c781e442f448751e32214db6b6e52a8de0976e207c157261d9d9bc448bec750")
    message(FATAL_ERROR "OCCT HLR patch output mismatch for HLRBRep_Data.cxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

agentcad_read_occt(HLRBRep_Data.hxx
  3fbb43a47faecd6b3323b07e15d6d6abe6adcf851ef35f1380e8109aac98fbb4
  93273d6438a091127624c760dfe42f8c1b94ddfe155d8fcc1a98d8ceeb3a2318)
if(NOT already_patched)
  set(before [==[//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

#ifndef _HLRBRep_Data_HeaderFile
#define _HLRBRep_Data_HeaderFile
]==])
  set(after [==[//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#ifndef _HLRBRep_Data_HeaderFile
#define _HLRBRep_Data_HeaderFile
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[                                             const double            p2);

  //! Classification of an edge.
  Standard_EXPORT TopAbs_State Classify(const int               E,
                                        const HLRBRep_EdgeData& ED,
                                        const bool              LevelFlag,
]==])
  set(after [==[                                             const double            p2);

  //! Classification of an edge.
  Standard_EXPORT TopAbs_State ClassifyAtPoint(const int E,
                                               const HLRBRep_EdgeData& ED,
                                               const double param);

  Standard_EXPORT TopAbs_State Classify(const int               E,
                                        const HLRBRep_EdgeData& ED,
                                        const bool              LevelFlag,
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[  DEFINE_STANDARD_RTTIEXT(HLRBRep_Data, Standard_Transient)

private:
  //! Orient the OutLines (left must be inside projection).
  //! Returns True if the face of a closed shell has been
  //! inverted.
]==])
  set(after [==[  DEFINE_STANDARD_RTTIEXT(HLRBRep_Data, Standard_Transient)

private:
  TopAbs_State ClassifyImpl(const int E, const HLRBRep_EdgeData& ED,
                           const bool LevelFlag, int& Level, const double param,
                           const bool CountLevels);

  //! Orient the OutLines (left must be inside projection).
  //! Returns True if the face of a closed shell has been
  //! inverted.
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "93273d6438a091127624c760dfe42f8c1b94ddfe155d8fcc1a98d8ceeb3a2318")
    message(FATAL_ERROR "OCCT HLR patch output mismatch for HLRBRep_Data.hxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

agentcad_read_occt(HLRBRep_Hider.cxx
  5843822d873229a5aee5e4cffa533f51044bfb925f5e96bb72493f437c843924
  e5970ed30dda007262d0380d5c5962c7e19dcc4a4e51ae492e182c4373279c26)
if(NOT already_patched)
  set(before [==[// commercial license or contractual agreement.

#define No_Standard_OutOfRange

#include <HLRAlgo_Coincidence.hxx>
#include <HLRBRep_Data.hxx>
]==])
  set(after [==[// commercial license or contractual agreement.

#define No_Standard_OutOfRange

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#include <HLRAlgo_Coincidence.hxx>
#include <HLRBRep_Data.hxx>
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[            {
              // int aNbp = 1;
              // aTestState = myDS->SimplClassify(E, ed, aNbp, p1, p2);
              int tmplevel = 0;
              aTestState   = myDS->Classify(E, ed, true, tmplevel, (p1 + p2) / 2.);
            }

            if (aTestState != TopAbs_OUT)
]==])
  set(after [==[            {
              // int aNbp = 1;
              // aTestState = myDS->SimplClassify(E, ed, aNbp, p1, p2);
              aTestState = myDS->ClassifyAtPoint(E, ed, (p1 + p2) / 2.);
            }

            if (aTestState != TopAbs_OUT)
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "e5970ed30dda007262d0380d5c5962c7e19dcc4a4e51ae492e182c4373279c26")
    message(FATAL_ERROR "OCCT HLR patch output mismatch for HLRBRep_Hider.cxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

# agentcad-hlr-midpoint-v2: stream qualified roots and retain both face grids.
agentcad_read_occt(HLRBRep_Intersector.cxx
  62887180787c819e6d9597c1ea15a8b84741132573cd5547f71853485152eb83
  41258b6390dc96374102ef57e7add7816b6141bd35d9cd4e20d94451b3795159)
if(NOT already_patched)
  set(before [==[  #define No_Exception
#endif

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#include <Bnd_Box.hxx>
#include <algorithm>
#include <ElCLib.hxx>
#include <gp_Lin.hxx>
#include <HLRBRep_CurveTool.hxx>
]==])
  set(after [==[  #define No_Exception
#endif

// AgentCAD 2026-10-07: stop existential root solving only after exact
// depth and trimmed-face qualification; keep count-dependent queries intact.

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.

#include <Bnd_Box.hxx>
#include <HLRBRep_LineTool.hxx>
#include <HLRBRep_TheCSFunctionOfInterCSurf.hxx>
#include <HLRBRep_TheExactInterCSurf.hxx>
#include <HLRBRep_TheInterferenceOfInterCSurf.hxx>
#include <math_FunctionSetRoot.hxx>
#include "../../TKGeomAlgo/IntCurveSurface/IntCurveSurface_InterUtils.pxx"
#include <algorithm>
#include <memory>
#include <ElCLib.hxx>
#include <gp_Lin.hxx>
#include <HLRBRep_CurveTool.hxx>
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[#include <IntRes2d_Position.hxx>
#include <IntRes2d_Transition.hxx>
#include <StdFail_UndefinedDerivative.hxx>

// #define PERF

]==])
  set(after [==[#include <IntRes2d_Position.hxx>
#include <IntRes2d_Transition.hxx>
#include <StdFail_UndefinedDerivative.hxx>

namespace
{
// Own the two unchanged sampling grids for the currently loaded face. HLR
// alternates dense existential checks and original-grid fallback/counting;
// rebuilding a grid at every switch needlessly reevaluates the same surface.
// The public intersector layout remains unchanged. Load destroys both grids.
class AgentcadFacePolyhedra : public HLRBRep_ThePolyhedronOfInterCSurf
{
public:
  AgentcadFacePolyhedra(HLRBRep_Surface* surface, int nu, int nv,
                       double u1, double v1, double u2, double v2)
    : HLRBRep_ThePolyhedronOfInterCSurf(surface, nu, nv, u1, v1, u2, v2) {}
  AgentcadFacePolyhedra(const AgentcadFacePolyhedra&) = delete;
  AgentcadFacePolyhedra& operator=(const AgentcadFacePolyhedra&) = delete;

  HLRBRep_ThePolyhedronOfInterCSurf* Select(HLRBRep_Surface* surface, int nu, int nv,
                                         double u1, double v1, double u2, double v2)
  {
    if (Matches(*this, nu, nv)) return this;
    if (!alternate || !Matches(*alternate, nu, nv))
      alternate = std::make_unique<HLRBRep_ThePolyhedronOfInterCSurf>(
        surface, nu, nv, u1, v1, u2, v2);
    return alternate.get();
  }

private:
  static bool Matches(const HLRBRep_ThePolyhedronOfInterCSurf& grid, int nu, int nv)
  {
    int oldU, oldV;
    grid.Size(oldU, oldV);
    return oldU == std::max(3, nu) && oldV == std::max(3, nv);
  }
  std::unique_ptr<HLRBRep_ThePolyhedronOfInterCSurf> alternate;
};
}

// #define PERF

]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[void HLRBRep_Intersector::Load(HLRBRep_Surface* theSurface)
{
  mySurface = theSurface;
  delete myPolyhedron;
  myPolyhedron = nullptr;
}

//=================================================================================================

void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P)
{
]==])
  set(after [==[void HLRBRep_Intersector::Load(HLRBRep_Surface* theSurface)
{
  mySurface = theSurface;
  delete static_cast<AgentcadFacePolyhedra*>(myPolyhedron);
  myPolyhedron = nullptr;
}

//=================================================================================================

namespace
{
class AgentcadOcclusionCandidates : public IntCurveSurface_Intersection
{
public:
  bool Accept(const IntCurveSurface_IntersectionPoint& point,
              const std::function<bool(const IntCurveSurface_IntersectionPoint&)>& predicate)
  {
    done = true;
    const int previous = NbPoints();
    Append(point);
    return NbPoints() > previous && predicate(Point(NbPoints()));
  }
};
}

void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P)
{
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[}

void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P, const bool theDense)
{
  myTypePerform                 = 2;
  const GeomAbs_SurfaceType typ = HLRBRep_SurfaceTool::GetType(mySurface);
]==])
  set(after [==[}

void HLRBRep_Intersector::Perform(const gp_Lin& L, const double P, const bool theDense)
{
  PerformImpl(L, P, theDense, nullptr);
}

bool HLRBRep_Intersector::PerformUntil(const gp_Lin& L, const double P, const bool theDense,
  const std::function<bool(const IntCurveSurface_IntersectionPoint&)>& theAccept)
{
  return PerformImpl(L, P, theDense, &theAccept);
}

bool HLRBRep_Intersector::PerformImpl(const gp_Lin& L, const double P, const bool theDense,
  const std::function<bool(const IntCurveSurface_IntersectionPoint&)>* theAccept)
{
  myTypePerform                 = 2;
  const GeomAbs_SurfaceType typ = HLRBRep_SurfaceTool::GetType(mySurface);
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[        nbsu = std::clamp(support.NbUKnots() * support.UDegree(), nbsu, 512);
        nbsv = std::clamp(support.NbVKnots() * support.VDegree(), nbsv, 512);
      }
      if (myPolyhedron != nullptr)
      {
        int oldU, oldV;
        myPolyhedron->Size(oldU, oldV);
        if (oldU != std::max(3, nbsu) || oldV != std::max(3, nbsv))
        {
          delete myPolyhedron;
          myPolyhedron = nullptr;
        }
      }
      if (myPolyhedron == nullptr)
        myPolyhedron = new HLRBRep_ThePolyhedronOfInterCSurf(mySurface, nbsu, nbsv, u1, v1, u2, v2);
      double x0, y0, z0, x1, y1, z1, pmin, pmax; //,pp;
      myPolyhedron->Bounding().Get(x0, y0, z0, x1, y1, z1);
      //-- On va rejeter tous les points de parametres > P
      double p;
      p    = ElCLib::Parameter(L, gp_Pnt(x0, y0, z0));
]==])
  set(after [==[        nbsu = std::clamp(support.NbUKnots() * support.UDegree(), nbsu, 512);
        nbsv = std::clamp(support.NbVKnots() * support.VDegree(), nbsv, 512);
      }
      if (myPolyhedron == nullptr)
        myPolyhedron = new AgentcadFacePolyhedra(mySurface, nbsu, nbsv, u1, v1, u2, v2);
      auto* polyhedron = static_cast<AgentcadFacePolyhedra*>(myPolyhedron)->Select(
        mySurface, nbsu, nbsv, u1, v1, u2, v2);
      double x0, y0, z0, x1, y1, z1, pmin, pmax; //,pp;
      polyhedron->Bounding().Get(x0, y0, z0, x1, y1, z1);
      //-- On va rejeter tous les points de parametres > P
      double p;
      p    = ElCLib::Parameter(L, gp_Pnt(x0, y0, z0));
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[        }
      }
      HLRBRep_ThePolygonOfInterCSurf Polygon(L, pmin, pmax, 3);
      myCSIntersector.Perform(L,
                              Polygon,
                              mySurface,
                              *((HLRBRep_ThePolyhedronOfInterCSurf*)myPolyhedron));

      break;
    }
]==])
  set(after [==[        }
      }
      HLRBRep_ThePolygonOfInterCSurf Polygon(L, pmin, pmax, 3);
      if (theAccept != nullptr)
      {
        // Same sorted brackets, duplicate suppression, exact solver and point
        // construction as OCCT ProcessSortedPoints. Only the unused remainder
        // of the root inventory is skipped after the caller qualifies a root.
        HLRBRep_TheInterferenceOfInterCSurf interference(Polygon, *polyhedron);
        HLRBRep_TheCSFunctionOfInterCSurf function(mySurface, L);
        HLRBRep_TheExactInterCSurf exact(function, IntCurveSurface_InterUtils::THE_TOLTANGENCY);
        math_FunctionSetRoot solver(exact.Function());
        IntCurveSurface_InterUtils::SortedStartPoints seeds;
        IntCurveSurface_InterUtils::CollectInterferencePoints(interference, *polyhedron, Polygon, seeds);
        IntCurveSurface_InterUtils::SortStartPoints(seeds);
        const double ptol = 10 * Precision::PConfusion();
        double su = 0, sv = 0, sw = 0;
        AgentcadOcclusionCandidates candidates;
        for (int i = 0; i < seeds.Size(); ++i)
        {
          double u = seeds.TabU(i), v = seeds.TabV(i), w = seeds.TabW(i);
          if (i == 0) su = u - 1;
          if (std::abs(u-su) > ptol || std::abs(v-sv) > ptol || std::abs(w-sw) > ptol)
          {
            exact.Perform(u, v, w, solver, u1, u2, v1, v2, Polygon.InfParameter(), Polygon.SupParameter());
            if (exact.IsDone() && !exact.IsEmpty())
            {
              w = exact.ParameterOnCurve();
              exact.ParameterOnSurface(u, v);
              IntCurveSurface_IntersectionPoint point;
              if (IntCurveSurface_InterUtils::ComputeAppendPoint<gp_Lin, HLRBRep_LineTool,
                    HLRBRep_Surface*, HLRBRep_SurfaceTool>(L, w, mySurface, u, v, point)
                  && candidates.Accept(point, *theAccept))
                return true;
            }
          }
          su = seeds.TabU(i); sv = seeds.TabV(i); sw = seeds.TabW(i);
        }
        return false;
      }
      myCSIntersector.Perform(L,
                              Polygon,
                              mySurface,
                              *polyhedron);

      break;
    }
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[    NbIntersCSVides++;
  }
#endif
}

//=================================================================================================
]==])
  set(after [==[    NbIntersCSVides++;
  }
#endif
  if (theAccept != nullptr && myCSIntersector.IsDone())
    for (int i = 1; i <= myCSIntersector.NbPoints(); ++i)
      if ((*theAccept)(myCSIntersector.Point(i))) return true;
  return false;
}

//=================================================================================================
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[
void HLRBRep_Intersector::Destroy()
{
  delete myPolyhedron;
  myPolyhedron = nullptr;
}

]==])
  set(after [==[
void HLRBRep_Intersector::Destroy()
{
  delete static_cast<AgentcadFacePolyhedra*>(myPolyhedron);
  myPolyhedron = nullptr;
}

]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "41258b6390dc96374102ef57e7add7816b6141bd35d9cd4e20d94451b3795159")
    message(FATAL_ERROR "OCCT streaming patch output mismatch for HLRBRep_Intersector.cxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

# agentcad-hlr-midpoint-v2: stream qualified roots and retain both face grids.
agentcad_read_occt(HLRBRep_Intersector.hxx
  2d83d4a6002d9a974f2f9131ba7d44baccbb2e3874b74ee4d155d3f36948ca7e
  21487af005a49a84cfb300f134eb441a8d9261fa9a632cc828090ffa299973b9)
if(NOT already_patched)
  set(before [==[#ifndef _HLRBRep_Intersector_HeaderFile
#define _HLRBRep_Intersector_HeaderFile

#include <Standard.hxx>
#include <Standard_DefineAlloc.hxx>

#include <Standard_Integer.hxx>
]==])
  set(after [==[#ifndef _HLRBRep_Intersector_HeaderFile
#define _HLRBRep_Intersector_HeaderFile

// AgentCAD 2026-10-07: stop existential root solving only after exact
// depth and trimmed-face qualification; keep count-dependent queries intact.

#define AGENTCAD_OCCT_HLR_STREAMING 1

#include <Standard.hxx>
#include <functional>
#include <Standard_DefineAlloc.hxx>

#include <Standard_Integer.hxx>
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[
  Standard_EXPORT void Perform(const gp_Lin& theL, const double theP, const bool theDense);

  Standard_EXPORT bool IsDone() const;

  Standard_EXPORT int NbPoints() const;
]==])
  set(after [==[
  Standard_EXPORT void Perform(const gp_Lin& theL, const double theP, const bool theDense);

  //! Existential query; does not publish a complete intersection inventory.
  Standard_EXPORT bool PerformUntil(const gp_Lin& theL, const double theP, const bool theDense,
    const std::function<bool(const IntCurveSurface_IntersectionPoint&)>& theAccept);

  Standard_EXPORT bool IsDone() const;

  Standard_EXPORT int NbPoints() const;
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[  ~HLRBRep_Intersector() { Destroy(); }

private:
  IntRes2d_IntersectionPoint         mySinglePoint;
  int                                myTypePerform;
  HLRBRep_CInter                     myIntersector;
]==])
  set(after [==[  ~HLRBRep_Intersector() { Destroy(); }

private:
  bool PerformImpl(const gp_Lin& theL, const double theP, const bool theDense,
    const std::function<bool(const IntCurveSurface_IntersectionPoint&)>* theAccept);
  IntRes2d_IntersectionPoint         mySinglePoint;
  int                                myTypePerform;
  HLRBRep_CInter                     myIntersector;
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "21487af005a49a84cfb300f134eb441a8d9261fa9a632cc828090ffa299973b9")
    message(FATAL_ERROR "OCCT streaming patch output mismatch for HLRBRep_Intersector.hxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()

# agentcad-hlr-midpoint-v2: stream qualified roots and retain both face grids.
agentcad_read_occt(HLRBRep_Data.cxx
  6c781e442f448751e32214db6b6e52a8de0976e207c157261d9d9bc448bec750
  397f666cd0bf1f7c1ce5ef6df7317a3edff9682190dd4d0396e8f1585ab447ca)
if(NOT already_patched)
  set(before [==[// commercial license or contractual agreement.

// #define No_Standard_OutOfRange

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.
]==])
  set(after [==[// commercial license or contractual agreement.

// #define No_Standard_OutOfRange

// AgentCAD 2026-10-07: stop existential root solving only after exact
// depth and trimmed-face qualification; keep count-dependent queries intact.

// AgentCAD 2026-10-06: bounded dense ray bracketing for existential
// midpoint checks, with original-grid fallback and unchanged root counting.
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  set(before [==[  gp_Lin L    = myProj.Shoot(Psta.X(), Psta.Y());
  const double rayLimit = ElCLib::Parameter(L, PLim);
  const bool denseFirst = !CountLevels && iFaceGeom->Surface().GetType() == GeomAbs_BSplineSurface;
  for (int attempt = 0; attempt < (denseFirst ? 2 : 1); ++attempt)
  {
  double wLim = rayLimit;
]==])
  set(after [==[  gp_Lin L    = myProj.Shoot(Psta.X(), Psta.Y());
  const double rayLimit = ElCLib::Parameter(L, PLim);
  const bool denseFirst = !CountLevels && iFaceGeom->Surface().GetType() == GeomAbs_BSplineSurface;
  if (!CountLevels || !LevelFlag)
  {
  double TolZ = myBigSize * 0.000001;
  if (iFaceTest)
    TolZ = (!myLEOutLine && !myLEInternal) ? myBigSize * 0.001 : myBigSize * 0.01;
  const double wLim = rayLimit - TolZ;
  const double PeriodU = iFaceGeom->IsUPeriodic() ? iFaceGeom->UPeriod() : 0.;
  const double PeriodV = iFaceGeom->IsVPeriodic() ? iFaceGeom->VPeriod() : 0.;
  const auto qualifies = [&](const IntCurveSurface_IntersectionPoint& point) {
    gp_Pnt p; double u, v, w; IntCurveSurface_TransitionOnCurve transition;
    point.Values(p, u, v, w, transition);
    if (!(w < wLim)) return false;
    double shift;
    if (PeriodU > 0.)
      GeomInt::AdjustPeriodic(u, iFaceGeom->FirstUParameter(), iFaceGeom->LastUParameter(), PeriodU, u, shift);
    if (PeriodV > 0.)
      GeomInt::AdjustPeriodic(v, iFaceGeom->FirstVParameter(), iFaceGeom->LastVParameter(), PeriodV, v, shift);
    return myClassifier->Classify(gp_Pnt2d(u, v), Precision::PConfusion()) != TopAbs_OUT;
  };
  for (int attempt = 0; attempt < (denseFirst ? 2 : 1); ++attempt)
  {
      if (myIntersector.PerformUntil(L, rayLimit, denseFirst && attempt == 0, qualifies))
      {
        Level = 1;
        return TopAbs_IN;
      }
  }
  return state;
  }
  for (int attempt = 0; attempt < (denseFirst ? 2 : 1); ++attempt)
  {
  double wLim = rayLimit;
]==])
  string(REPLACE "${before}" "${after}" modified "${modified}")
  string(SHA256 result_sha256 "${modified}")
  if(NOT result_sha256 STREQUAL "397f666cd0bf1f7c1ce5ef6df7317a3edff9682190dd4d0396e8f1585ab447ca")
    message(FATAL_ERROR "OCCT streaming patch output mismatch for HLRBRep_Data.cxx: ${result_sha256}")
  endif()
  file(WRITE "${source}" "${modified}")
endif()
