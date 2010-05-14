/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkFEMDiscConformalMap.h,v $
  Language:  C++
  Date:      $Date: 2006/10/13 19:20:46 $
  Version:   $Revision: 1.2 $

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _FEMDiscConformalMap_h
#define _FEMDiscConformalMap_h

#include <vnl/vnl_matops.h>
#include <vnl/algo/vnl_svd.h>
#include <vnl/algo/vnl_symmetric_eigensystem.h>

#include "vtkDataSetWriter.h"
#include "vtkDataSetMapper.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkActor.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkDataSetReader.h"
#include "vtkUnstructuredGrid.h"
#include "vtkDataSet.h"
#include "vtkCellArray.h"

#include "vtkVolume16Reader.h"
#include "vtkImageReader2.h"
#include "vtkPolyDataMapper.h"
#include "vtkActor.h"
#include "vtkOutlineFilter.h"
#include "vtkCamera.h"
#include "vtkProperty.h"
#include "vtkPolyData.h"
#include "vtkPolyVertex.h"
#include "vtkPointData.h"
#include "vtkExtractEdges.h"
#include "vtkPolyDataNormals.h"
#include "vtkMarchingCubes.h"
#include "vtkImageGaussianSmooth.h"
#include "vtkDecimatePro.h"
#include "vtkContourFilter.h"
#include "vtkPolyDataConnectivityFilter.h"
//#include "vtkKitwareContourFilter.h"
#include "vtkSmoothPolyDataFilter.h"
#include "vtkSTLWriter.h"
#include "vtkUnstructuredGridToPolyDataFilter.h"
//#include "itkImageToVTKImageFilter.h"


#include "itkDijkstrasAlgorithm.h"
#include "itkManifoldIntegrationAlgorithm.h"
//#include "itkTriangulatedDijkstrasAlgorithm.h"

#include "itkObject.h"
#include "itkProcessObject.h"

#include "itkVectorContainer.h"
#include "itkCastImageFilter.h"

#include "itkFEM.h"
#include "itkFEMLinearSystemWrapperItpack.h"

#include "itkMesh.h"


namespace itk
{


/** \class FEMDiscConformalMap
 * Avants Epstein conformal mapping algorithm using FEM.
 *
 * \note The origin of a neighborhood is always taken to be 
 *       the first point entered into and the 
 *       last point stored in the list.
 */
template < typename TSurface, typename TImage, unsigned int TDimension=3 >
           class FEMDiscConformalMap : public ProcessObject
{
public:

  /** Standard class typedefs. */
  typedef FEMDiscConformalMap Self;
  typedef ProcessObject Superclass;
  typedef SmartPointer<Self> Pointer;
  typedef SmartPointer<const Self>  ConstPointer;

  /** Run-time type information (and related methods). */
  itkTypeMacro(FEMDiscConformalMap,ProcessObject);

  /** Method for creation through the object factory. */
  itkNewMacro(Self);

  /** Surface (mesh) types. */
  typedef TImage               ImageType;
  typedef typename TImage::Pointer      ImageTypePointer;
  typedef typename TImage::IndexType      IndexType;

  /** Surface (mesh) types. */
  typedef TSurface               SurfaceType;
  typedef SurfaceType* SurfaceTypePointer;
//  typedef typename SurfaceType::PointType   PointType;
//  typedef typename SurfaceType::CellsContainerPointer InputCellsContainerPointer;
//  typedef typename SurfaceType::CellsContainer::Iterator  InputCellsContainerIterator;

  /** Image dimension. */
//  itkStaticConstMacro(ImageDimension, unsigned int, TImage::ImageDimension);
  itkStaticConstMacro(ImageDimension, unsigned int, TDimension);
  itkStaticConstMacro(SurfaceDimension, unsigned int, TDimension);

  typedef float                     RealType;
  typedef vnl_vector<RealType>      VectorType;
  typedef vnl_vector_fixed<RealType,itkGetStaticConstMacro(ImageDimension)>   
    FixedVectorType;
  typedef vnl_matrix<double>        MatrixType;
  
  typedef  Image<float,2>   FlatImageType;
  typedef  typename FlatImageType::Pointer FlatImageTypePointer;
  typedef  GraphSearchNode<float,float,3>        GraphSearchNode;
  typedef  typename GraphSearchNode::Pointer        GraphSearchNodePointer;
  typedef  typename GraphSearchNode::NodeLocationType        NodeLocationType;
  typedef  ManifoldIntegrationAlgorithm<GraphSearchNode>  ManifoldIntegratorType;
//  typedef  TriangulatedDijkstrasAlgorithm<GraphSearchNode>  ManifoldIntegratorType;
  typedef  typename ManifoldIntegratorType::Pointer  ManifoldIntegratorTypePointer;

  /** FEM types */
  typedef itk::fem::MaterialLinearElasticity MaterialType;
  typedef itk::fem::Node NodeType;
  typedef itk::fem::LoadNode LoadType;
  typedef itk::fem::Element3DC0LinearTriangularLaplaceBeltrami ElementType;
  typedef itk::fem::Element3DC0LinearTriangularMembrane ElementType1;


  /** Set input parameter file */
  itkSetStringMacro( ParameterFileName );

  /** Set input parameter file */
  itkGetStringMacro( ParameterFileName );

  itkGetMacro(Sigma, RealType);
  itkSetMacro(Sigma, RealType);

  itkGetMacro(SurfaceMesh, SurfaceTypePointer);
  itkSetMacro(SurfaceMesh, SurfaceTypePointer);

  void SetDebug(bool b) {m_Debug=b;}
  void SetReadFromFile(bool b) {m_ReadFromFile=b;}

  void FixPointsBeyondDisc();
  void FixPointsAlongRadialLine();
  void FixThetaAlongBorder();
  void ConformalMap();
  void ConformalMap2();
  void ConformalMap3();
  void ConjugateHarmonic();

  bool InBorder(GraphSearchNodePointer);
  bool InDisc(GraphSearchNodePointer);

  void   ExtractSurfaceDisc();
  void   BuildOutputMeshes(float tval = 0.0);
  
  SurfaceTypePointer            m_ExtractedSurfaceMesh;
  SurfaceTypePointer            m_DiskSurfaceMesh;

  void SetSmooth(float i){ m_Smooth=i;}

  void MeasureLengthDistortion();
  
  void FindSource(IndexType);
  void MakeFlatImage();
  FlatImageTypePointer          m_FlatImage;

  void MapToSquare();

protected:


  bool GenerateSystemFromSurfaceMesh();
  void ApplyRealForces();
  void ApplyImaginaryForces();

  FEMDiscConformalMap();
  virtual ~FEMDiscConformalMap(){};


private:

  std::vector<int>              m_DiscBoundaryList; // contains ids of nodes at boundary
  RealType                      m_Sigma;
  RealType                      m_Pi;
  std::string                   m_ParameterFileName;
  int                           m_NorthPole;
  int                           m_SourceNodeNumber;

  itk::fem::Solver              m_Solver;

  bool                          m_ReadFromFile;
  bool                          m_Debug;
  bool                          m_FindingRealSolution;

  VectorType                    m_RealSolution;
  VectorType                    m_ImagSolution;
  VectorType                    m_Radius;
  
  SurfaceTypePointer            m_SurfaceMesh;

  float                         m_MaxCost;
  itk::fem::LinearSystemWrapperItpack itpackWrapper; 
  
  unsigned long                 m_PoleElementsGN[7];
  ManifoldIntegratorTypePointer manifoldIntegrator;

  float m_Smooth;
};



} // namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#include "itkFEMDiscConformalMap.cxx"
#endif

#endif
