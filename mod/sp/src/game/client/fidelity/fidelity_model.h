//========= mapbase-fidelity ==================================================//
//
// Purpose: The vertices and triangles of a model as it is drawn, for the
//          analyses of the glow lights and the liquid.
//
//=============================================================================//

#ifndef FIDELITY_MODEL_H
#define FIDELITY_MODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"

struct vertexFileHeader_t;
struct mstudiovertex_t;
struct studiohdr_t;
struct studiohwdata_t;
struct studioloddata_t;
struct model_t;
class IMaterial;

//-----------------------------------------------------------------------------
// The vertices of the root LOD in the order the meshes index them
// (mstudiomodel_t::vertexindex, mstudiomesh_t::vertexoffset). Models with
// LODs keep them in pieces in the .vvd, put together by its fixup table;
// reading the .vvd straight gave other parts' vertices (seen in game: the
// chargers' light sat beside the model). False if there is no vertex data.
//-----------------------------------------------------------------------------
bool Fidelity_ModelVertices( const vertexFileHeader_t *pVertexHdr, int nRootLOD, CUtlVector< const mstudiovertex_t * > &vertices );

// Does a mesh's skin reference use pMaterial in any skin family (a prop set
// to another skin in Hammer)?
bool Fidelity_SkinRefUses( const studiohdr_t *pStudioHdr, const studioloddata_t &lod, int nSkinRef, IMaterial *pMaterial );

//-----------------------------------------------------------------------------
// The triangles drawn with pMaterial (root LOD), as indices into the model's
// vertex list (Fidelity_ModelVertices) - three per triangle. Read from the
// model's .dx90.vtx, every offset checked; false if it can not be read.
//-----------------------------------------------------------------------------
bool Fidelity_ModelTriangles( const model_t *pModel, const studiohdr_t *pStudioHdr, const studiohwdata_t *pHardware,
	IMaterial *pMaterial, CUtlVector< int > &triangles );

#endif // FIDELITY_MODEL_H
