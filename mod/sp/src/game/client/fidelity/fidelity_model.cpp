//========= mapbase-fidelity ==================================================//
//
// Purpose: The vertices of a model as it is drawn (root LOD, with the .vvd's
//          fixup table), like the engine's Studio_LoadVertexes, and its
//          triangles (from the .dx90.vtx).
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_model.h"
#include "studio.h"
#include "optimize.h"
#include "filesystem.h"
#include "materialsystem/imaterial.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool Fidelity_ModelVertices( const vertexFileHeader_t *pVertexHdr, int nRootLOD, CUtlVector< const mstudiovertex_t * > &vertices )
{
	vertices.RemoveAll();
	if ( !pVertexHdr || nRootLOD < 0 || nRootLOD >= MAX_NUM_LODS )
		return false;
	const mstudiovertex_t *pData = pVertexHdr->GetVertexData();
	if ( !pData )
		return false;

	if ( pVertexHdr->numFixups <= 0 )
	{
		// One block, in mesh order
		const int nCount = pVertexHdr->numLODVertexes[ nRootLOD ];
		vertices.EnsureCapacity( nCount );
		for ( int i = 0; i < nCount; i++ )
			vertices.AddToTail( pData + i );
		return true;
	}

	// Pieces for the LODs: the ones that belong to the root LOD, one after the
	// other. The engine usually hands out the vertices already fixed up
	// (numFixups 0); the table is still read with every range clamped to the
	// vertices there are, so a broken file can not run away.
	const vertexFileFixup_t *pFixups = (const vertexFileFixup_t *)( (const byte *)pVertexHdr + pVertexHdr->fixupTableStart );
	const int nTotal = pVertexHdr->numLODVertexes[ 0 ];
	const int nFixups = MIN( pVertexHdr->numFixups, nTotal );
	for ( int i = 0; i < nFixups && vertices.Count() < nTotal; i++ )
	{
		if ( pFixups[ i ].lod < nRootLOD )
			continue;
		const int nStart = MAX( pFixups[ i ].sourceVertexID, 0 );
		const int nEnd = (int)MIN( (int64)pFixups[ i ].sourceVertexID + MAX( pFixups[ i ].numVertexes, 0 ), (int64)nTotal );
		for ( int nSource = nStart; nSource < nEnd; nSource++ )
			vertices.AddToTail( pData + nSource );
	}
	return vertices.Count() > 0;
}

// Does a mesh's skin reference use pMaterial in any skin family (a prop set
// to another skin in Hammer)?
bool Fidelity_SkinRefUses( const studiohdr_t *pStudioHdr, const studioloddata_t &lod, int nSkinRef, IMaterial *pMaterial )
{
	for ( int nFamily = 0; nFamily < MAX( pStudioHdr->numskinfamilies, 1 ); nFamily++ )
	{
		const int nTexture = pStudioHdr->pSkinref( nFamily * pStudioHdr->numskinref )[ nSkinRef ];
		if ( nTexture >= 0 && nTexture < lod.numMaterials && lod.ppMaterials[ nTexture ] == pMaterial )
			return true;
	}
	return false;
}

// A T at nOffset of the file, or NULL if it does not fit (the .vtx may come from any mod)
template< typename T >
static const T *Fidelity_VtxAt( const CUtlVector< unsigned char > &file, int64 nOffset, int nCount = 1 )
{
	if ( nOffset < 0 || nCount < 0 || nOffset + (int64)sizeof( T ) * nCount > file.Count() )
		return NULL;
	return (const T *)( file.Base() + nOffset );
}

//-----------------------------------------------------------------------------
// The triangles drawn with pMaterial (root LOD), as indices into the model's
// vertex list (Fidelity_ModelVertices) - three per triangle. Read from the
// model's .dx90.vtx; false if it can not be read.
//-----------------------------------------------------------------------------
bool Fidelity_ModelTriangles( const model_t *pModel, const studiohdr_t *pStudioHdr, const studiohwdata_t *pHardware,
	IMaterial *pMaterial, CUtlVector< int > &triangles )
{
	triangles.RemoveAll();
	char szPath[ MAX_PATH ];
	Q_StripExtension( modelinfo->GetModelName( pModel ), szPath, sizeof( szPath ) );
	Q_strncat( szPath, ".dx90.vtx", sizeof( szPath ), COPY_ALL_CHARACTERS );
	CUtlVector< unsigned char > file;
	FileHandle_t hFile = filesystem->Open( szPath, "rb", "GAME" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return false;
	const int nSize = (int)filesystem->Size( hFile );
	if ( nSize > 0 && nSize <= 16 * 1024 * 1024 )
	{
		file.SetCount( nSize );
		if ( filesystem->Read( file.Base(), nSize, hFile ) != nSize )
			file.RemoveAll();
	}
	filesystem->Close( hFile );

	const OptimizedModel::FileHeader_t *pFile = Fidelity_VtxAt< OptimizedModel::FileHeader_t >( file, 0 );
	if ( !pFile || pFile->version != OPTIMIZED_MODEL_FILE_VERSION || pFile->numBodyParts != pStudioHdr->numbodyparts )
		return false;
	const studioloddata_t &lod = pHardware->m_pLODs[ pHardware->m_RootLOD ];
	const int nLOD = pHardware->m_RootLOD;

	for ( int nPart = 0; nPart < pFile->numBodyParts; nPart++ )
	{
		const int64 nPartAt = (int64)pFile->bodyPartOffset + nPart * (int64)sizeof( OptimizedModel::BodyPartHeader_t );
		const OptimizedModel::BodyPartHeader_t *pPart = Fidelity_VtxAt< OptimizedModel::BodyPartHeader_t >( file, nPartAt );
		const mstudiobodyparts_t *pStudioPart = pStudioHdr->pBodypart( nPart );
		if ( !pPart || pPart->numModels != pStudioPart->nummodels )
			return false;
		for ( int nModel = 0; nModel < pPart->numModels; nModel++ )
		{
			const int64 nModelAt = nPartAt + pPart->modelOffset + nModel * (int64)sizeof( OptimizedModel::ModelHeader_t );
			const OptimizedModel::ModelHeader_t *pVtxModel = Fidelity_VtxAt< OptimizedModel::ModelHeader_t >( file, nModelAt );
			const mstudiomodel_t *pSubModel = pStudioPart->pModel( nModel );
			if ( !pVtxModel || nLOD >= pVtxModel->numLODs )
				return false;
			const int64 nLodAt = nModelAt + pVtxModel->lodOffset + nLOD * (int64)sizeof( OptimizedModel::ModelLODHeader_t );
			const OptimizedModel::ModelLODHeader_t *pLod = Fidelity_VtxAt< OptimizedModel::ModelLODHeader_t >( file, nLodAt );
			if ( !pLod || pLod->numMeshes != pSubModel->nummeshes )
				return false;
			const int nBase = pSubModel->vertexindex / sizeof( mstudiovertex_t );
			for ( int nMesh = 0; nMesh < pLod->numMeshes; nMesh++ )
			{
				const mstudiomesh_t *pStudioMesh = pSubModel->pMesh( nMesh );
				if ( pStudioMesh->material < 0 || pStudioMesh->material >= pStudioHdr->numskinref ||
					!Fidelity_SkinRefUses( pStudioHdr, lod, pStudioMesh->material, pMaterial ) )
					continue;
				const int64 nMeshAt = nLodAt + pLod->meshOffset + nMesh * (int64)sizeof( OptimizedModel::MeshHeader_t );
				const OptimizedModel::MeshHeader_t *pMesh = Fidelity_VtxAt< OptimizedModel::MeshHeader_t >( file, nMeshAt );
				if ( !pMesh )
					return false;
				for ( int nGroup = 0; nGroup < pMesh->numStripGroups; nGroup++ )
				{
					const int64 nGroupAt = nMeshAt + pMesh->stripGroupHeaderOffset + nGroup * (int64)sizeof( OptimizedModel::StripGroupHeader_t );
					const OptimizedModel::StripGroupHeader_t *pGroup = Fidelity_VtxAt< OptimizedModel::StripGroupHeader_t >( file, nGroupAt );
					if ( !pGroup )
						return false;
					const OptimizedModel::Vertex_t *pVerts = Fidelity_VtxAt< OptimizedModel::Vertex_t >( file, nGroupAt + pGroup->vertOffset, pGroup->numVerts );
					const unsigned short *pIndices = Fidelity_VtxAt< unsigned short >( file, nGroupAt + pGroup->indexOffset, pGroup->numIndices );
					if ( !pVerts || !pIndices )
						return false;
					for ( int nStrip = 0; nStrip < pGroup->numStrips; nStrip++ )
					{
						const OptimizedModel::StripHeader_t *pStrip = Fidelity_VtxAt< OptimizedModel::StripHeader_t >( file,
							nGroupAt + pGroup->stripOffset + nStrip * (int64)sizeof( OptimizedModel::StripHeader_t ) );
						if ( !pStrip || pStrip->indexOffset < 0 || pStrip->numIndices < 0 || pStrip->indexOffset + pStrip->numIndices > pGroup->numIndices )
							return false;
						const bool bList = ( pStrip->flags & OptimizedModel::STRIP_IS_TRILIST ) != 0;
						const int nTriangles = bList ? pStrip->numIndices / 3 : pStrip->numIndices - 2;
						for ( int t = 0; t < nTriangles; t++ )
						{
							const int nFirst = pStrip->indexOffset + ( bList ? t * 3 : t );
							for ( int c = 0; c < 3; c++ )
							{
								const int nIndex = pIndices[ nFirst + c ];
								if ( nIndex >= pGroup->numVerts )
									return false;
								triangles.AddToTail( nBase + pStudioMesh->vertexoffset + pVerts[ nIndex ].origMeshVertID );
							}
						}
					}
				}
			}
		}
	}
	return triangles.Count() >= 3;
}
