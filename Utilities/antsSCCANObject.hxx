/*=========================================================================


  Program:   Advanced Normalization Tools
  Module:    $RCSfile: antsSCCANObject.txx,v $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) ConsortiumOfANTS. All rights reserved.
  See accompanying COPYING.txt or
  http://sourceforge.net/projects/advants/files/ANTS/ANTSCopyright.txt
  for details.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "itkMinimumMaximumImageFilter.h"
#include "itkConnectedComponentImageFilter.h"
#include "itkRelabelComponentImageFilter.h"
#include <vnl/vnl_random.h>
#include <vnl/vnl_trace.h>
#include <vnl/algo/vnl_ldl_cholesky.h>
#include <vnl/algo/vnl_qr.h>
#include <vnl/algo/vnl_matrix_inverse.h>
#include <vnl/algo/vnl_generalized_eigensystem.h>
#include "antsSCCANObject.h"
#include <time.h>
#include "itkCSVNumericObjectFileWriter.h"
#include "itkCSVArray2DDataObject.h"
#include "itkCSVArray2DFileReader.h"
#include "itkGradientMagnitudeRecursiveGaussianImageFilter.h"
#include "itkLaplacianRecursiveGaussianImageFilter.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkSurfaceImageCurvature.h"
#include "itkImageFileWriter.h"

namespace itk
{
namespace ants
{
template <class TInputImage, class TRealType>
antsSCCANObject<TInputImage, TRealType>::antsSCCANObject()
{
  this->m_UseL1 = true;
  this->m_MinClusterSizeP = 1;
  this->m_MinClusterSizeQ = 1;
  this->m_KeptClusterSize = 0;
  this->m_Debug = false;
  this->m_Silent = false;
  this->m_CorrelationForSignificanceTest = 0;
  this->m_SpecializationForHBM2011 = false;
  this->m_AlreadyWhitened = false;
  this->m_PinvTolerance = 1.e-6;
  this->m_PercentVarianceForPseudoInverse = 0.9;
  this->m_MaximumNumberOfIterations = 20;
  this->m_MaskImageP = NULL;
  this->m_MaskImageQ = NULL;
  this->m_MaskImageR = NULL;
  this->m_KeepPositiveP = true;
  this->m_KeepPositiveQ = true;
  this->m_KeepPositiveR = true;
  this->m_FractionNonZeroP = 0.5;
  this->m_FractionNonZeroQ = 0.5;
  this->m_FractionNonZeroR = 0.5;
  this->m_ConvergenceThreshold = 1.e-6;
  this->m_Epsilon = 1.e-12;
  this->m_GradStep = 0.1;
  this->m_UseLongitudinalFormulation = 0;
  this->m_RowSparseness = 0;
  this->m_Smoother = 0;
  this->m_Covering = false;
}

template <class TInputImage, class TRealType>
typename TInputImage::Pointer
antsSCCANObject<TInputImage, TRealType>
::ConvertVariateToSpatialImage(  typename antsSCCANObject<TInputImage,
                                                          TRealType>::VectorType w_p,
                                 typename TInputImage::Pointer mask,
                                 bool threshold_at_zero  )
{
  typename TInputImage::Pointer weights = TInputImage::New();
  weights->SetOrigin( mask->GetOrigin() );
  weights->SetSpacing( mask->GetSpacing() );
  weights->SetRegions( mask->GetLargestPossibleRegion() );
  weights->SetDirection( mask->GetDirection() );
  weights->Allocate();
  weights->FillBuffer( itk::NumericTraits<PixelType>::Zero );

  // overwrite weights with vector values;
  unsigned long vecind = 0;
  typedef itk::ImageRegionIteratorWithIndex<TInputImage> Iterator;
  Iterator mIter(mask, mask->GetLargestPossibleRegion() );
  for(  mIter.GoToBegin(); !mIter.IsAtEnd(); ++mIter )
    {
    if( mIter.Get() >= 0.5 )
      {
      TRealType val = 0;
      if( vecind < w_p.size() )
        {
        val = w_p(vecind);
        }
      else
        {
        ::ants::antscout << "vecind too large " << vecind << " vs " << w_p.size() << std::endl;
        ::ants::antscout << " this is likely a mask problem --- exiting! " << std::endl;
        std::exception();
        }
      if( threshold_at_zero && fabs(val) > 0  )
        {
        weights->SetPixel(mIter.GetIndex(), 1);
        }
      else
        {
        weights->SetPixel(mIter.GetIndex(), val);
        }
      vecind++;
      }
    else
      {
      mIter.Set(0);
      }
    }

  return weights;
}

template <class TInputImage, class TRealType>
TRealType
antsSCCANObject<TInputImage, TRealType>
::CurvatureSparseness(
  typename antsSCCANObject<TInputImage, TRealType>::VectorType& x,
  TRealType sparsenessgoal, unsigned int maxit ,  typename TInputImage::Pointer mask )
// , typename antsSCCANObject<TInputImage,TRealType>::MatrixType& A,
//  typename antsSCCANObject<TInputImage,TRealType>::VectorType& b,
{
  if( mask.IsNull() )
    {
    return 0;
    }
  /** penalize by curvature */
  VectorType   signvec( x );
  RealType     kappa = 20;
  RealType     lastkappa = 20;
  unsigned int kkk = 0;
  unsigned int zct = 0;
  RealType     dkappa = 1;
  RealType     sp = 0;
  bool         notdone = true;
  unsigned int nzct = 0;

  while( notdone )
    {
    kappa = 0;
    VectorType gradvec = this->ComputeVectorLaplacian( x, mask );
    nzct = 0;
    for( unsigned int kk = 0; kk < x.size(); kk++ )
      {
      RealType grad = ( gradvec[kk]  ) * 0.5;
      if( ( ( x[kk] > 0 ) && ( signvec[kk] < 0 ) ) ||
          ( ( x[kk] < 0 ) && ( signvec[kk] > 0 ) ) )
        {
        x[kk] = 0;
        zct++;
        }
      else if( vnl_math_abs( x[kk] ) > 1.e-6 )
        {
        kappa += vnl_math_abs( grad );
        x[kk] = x[kk] + grad;
        nzct++;
        }
      else
        {
        x[kk] = 0; zct++;
        }
      }
    if( nzct > 0 )
      {
      kappa /= ( RealType ) nzct;
      }
    sp = ( RealType ) ( x.size() - nzct ) / x.size() * 100.0;
    //    VectorType pp = A.transpose() * ( A * x );
    //  RealType err = ( pp - b ).one_norm() / x.size();
    //    kappa = err  + this->m_FractionNonZeroP * ( 100 - sp);
    dkappa = lastkappa - kappa;
    lastkappa = kappa;
    if( kkk > maxit || sp > sparsenessgoal )
      {
      notdone = false;
      }
    if( kkk < 2 )
      {
      notdone = true;
      }
    kkk++;
    //    if ( ! notdone )
    //  ::ants::antscout  << " Kappa " << kappa << " " << kkk << " sparseness " << sp <<  " dkap " << dkappa << " nzct "
    // << nzct << std::endl;
    }

  VectorType gradvec = this->ComputeVectorGradMag( x, mask );
  //  ::ants::antscout  << " Kappa " << kappa << " " << kkk << " sparseness " << sp <<  " dkap " << dkappa << " nzct "
  // << nzct << " GradNorm " << gradvec.two_norm() << std::endl;
  return gradvec.two_norm();
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::ClusterThresholdVariate(  typename antsSCCANObject<TInputImage,
                                                     TRealType>::VectorType& w_p, typename TInputImage::Pointer mask,
                            unsigned int minclust )
{
  if( minclust <= 1 || mask.IsNull() )
    {
    return w_p;
    }
  typedef unsigned long                                                    ULPixelType;
  typedef itk::Image<ULPixelType, ImageDimension>                          labelimagetype;
  typedef TInputImage                                                      InternalImageType;
  typedef itk::ImageRegionIteratorWithIndex<ImageType>                     fIterator;
  typedef itk::ImageRegionIteratorWithIndex<labelimagetype>                Iterator;
  typedef itk::ConnectedComponentImageFilter<TInputImage, labelimagetype>  FilterType;
  typedef itk::RelabelComponentImageFilter<labelimagetype, labelimagetype> RelabelType;

// we assume w_p has been thresholded by another function
  bool threshold_at_zero = true;
  typename TInputImage::Pointer image = this->ConvertVariateToSpatialImage( w_p, mask, threshold_at_zero );
  typename FilterType::Pointer filter = FilterType::New();
  typename RelabelType::Pointer relabel = RelabelType::New();
  filter->SetInput( image );
  filter->SetFullyConnected( 0 );
  relabel->SetInput( filter->GetOutput() );
  relabel->SetMinimumObjectSize( 1 );
  try
    {
    relabel->Update();
    }
  catch( itk::ExceptionObject & excep )
    {
    ::ants::antscout << "Relabel: exception caught !" << std::endl;
    ::ants::antscout << excep << std::endl;
    }

  Iterator                   vfIter( relabel->GetOutput(),  relabel->GetOutput()->GetLargestPossibleRegion() );
  float                      maximum = relabel->GetNumberOfObjects();
  std::vector<unsigned long> histogram( (int)maximum + 1);
  for( int i = 0; i <= maximum; i++ )
    {
    histogram[i] = 0;
    }
  for(  vfIter.GoToBegin(); !vfIter.IsAtEnd(); ++vfIter )
    {
    float vox = vfIter.Get();
    if( vox > 0 )
      {
      if( vox > 0 )
        {
        histogram[(unsigned long)vox] = histogram[(unsigned long)vox] + 1;
        }
      }
    }

  // get the largest component's size
  unsigned long largest_component_size = 0;
  for( int i = 0; i <= maximum; i++ )
    {
    if( largest_component_size < histogram[i] )
      {
      largest_component_size = histogram[i];
      }
    }

  if(  largest_component_size < minclust )
    {
    minclust = largest_component_size - 1;
    }
//  now create the output vector
// iterate through the image and set the voxels where  countinlabel[(unsigned
// long)(labelimage->GetPixel(vfIter.GetIndex()) - min)]
// is < MinClusterSize
  unsigned long vecind = 0, keepct = 0;
  fIterator     mIter( mask,  mask->GetLargestPossibleRegion() );
  for(  mIter.GoToBegin(); !mIter.IsAtEnd(); ++mIter )
    {
    if( mIter.Get() > 0 )
      {
      float         vox = mask->GetPixel(vfIter.GetIndex() );
      unsigned long clustersize = 0;
      if( vox >= 0  )
        {
        clustersize = histogram[(unsigned long)(relabel->GetOutput()->GetPixel(mIter.GetIndex() ) )];
        if( clustersize > minclust )
          {
          keepct += 1;
          }                                      // get clusters > minclust
        //    if ( clustersize == largest_component_size ) { keepct++; } // get largest cluster
        else
          {
          w_p(vecind) = 0;
          }
        vecind++;
        }
      }
    }
  this->m_KeptClusterSize = histogram[1]; // only records the size of the largest cluster in the variate
  //  for (unsigned int i=0; i<histogram.size(); i++)
  //  if ( histogram[i] > minclust ) this->m_KeptClusterSize+=histogram[i];
  //  ::ants::antscout << " Cluster Threshold Kept % of sparseness " <<  ( (float)keepct/(float)w_p.size() ) /
  // this->m_FractionNonZeroP   << " kept clust size " << keepct << std::endl;
  return w_p;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::ConvertImageToVariate(  typename TInputImage::Pointer image, typename TInputImage::Pointer mask )
{
  typedef unsigned long                                ULPixelType;
  typedef itk::ImageRegionIteratorWithIndex<ImageType> Iterator;

  ULPixelType maskct = 0;
  Iterator    vfIter( mask, mask->GetLargestPossibleRegion() );
  for(  vfIter.GoToBegin(); !vfIter.IsAtEnd(); ++vfIter )
    {
    RealType maskval = vfIter.Get();
    if( maskval > 0 )
      {
      maskct++;
      }
    }
  VectorType vec( maskct );
  vec.fill( 0 );
  maskct = 0;
  for(  vfIter.GoToBegin(); !vfIter.IsAtEnd(); ++vfIter )
    {
    RealType maskval = vfIter.Get();
    RealType imageval = image->GetPixel( vfIter.GetIndex() );
    if( maskval > 0 )
      {
      vec[maskct] = imageval;
      maskct++;
      }
    }

  return vec;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::InitializeV( typename antsSCCANObject<TInputImage, TRealType>::MatrixType p, unsigned long seed )
{
  VectorType w_p( p.columns() );

  w_p.fill(0);
  for( unsigned int its = 0; its < 1; its++ )
    {
    vnl_random randgen( seed ); /* use constant seed to prevent weirdness */
    for( unsigned long i = 0; i < p.columns(); i++ )
      {
      if( seed > 0 )
        {
        w_p(i) = randgen.normal();
        // w_p(i)=randgen.drand32();
        }
      else
        {
        w_p(i) = 1.0;
        }
      }
    }
  w_p = w_p / p.columns();
  return w_p;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::SpatiallySmoothVector( typename antsSCCANObject<TInputImage, TRealType>::VectorType vec,
                         typename TInputImage::Pointer mask, bool surface  )
{
  if( mask.IsNull() || this->m_Smoother < 1.e-9 )
    {
    return vec;
    } 
  ImagePointer image = this->ConvertVariateToSpatialImage( vec, mask, false );
  typename TInputImage::SizeType dim =
    mask->GetLargestPossibleRegion().GetSize();
  if ( ( surface )  && ( dim[2] == 3 ) )
    {
  unsigned int sigma = ( unsigned int ) this->m_Smoother;
  if (  this->m_Smoother > 0.0001 )
    if ( sigma < 1 ) sigma = 1;
  typedef itk::SurfaceImageCurvature<TInputImage> ParamType;
  typename ParamType::Pointer Parameterizer = ParamType::New();
  Parameterizer->SetInputImage(mask);
  Parameterizer->SetFunctionImage(image);
  Parameterizer->SetNeighborhoodRadius( 2 );
  Parameterizer->SetSigma( 1.0 );
  Parameterizer->SetUseGeodesicNeighborhood(false);
  Parameterizer->SetUseLabel(false);
  Parameterizer->SetThreshold(0.5);
  Parameterizer->IntegrateFunctionOverSurface(true);
  for( unsigned int i = 0; i < sigma; i++ )
    {
    Parameterizer->IntegrateFunctionOverSurface(true);
    }
  VectorType svec = this->ConvertImageToVariate( Parameterizer->GetFunctionImage(),  mask );
  return svec;
    }
  else 
    {
      RealType     spacingsize = 0;
      for( unsigned int d = 0; d < ImageDimension; d++ )
	{
	  RealType sp = mask->GetSpacing()[d];
	  spacingsize += sp * sp;
	}
      spacingsize = sqrt( spacingsize );
      typedef itk::DiscreteGaussianImageFilter<ImageType, ImageType> dgf;
      typename dgf::Pointer filter = dgf::New();
      filter->SetUseImageSpacingOn();
      filter->SetVariance(  this->m_Smoother * spacingsize );
      filter->SetMaximumError( .01f );
      filter->SetInput( image );
      filter->Update();
      VectorType gradvec = this->ConvertImageToVariate( filter->GetOutput(),  mask );
      return gradvec;
    }
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::ComputeVectorLaplacian( typename antsSCCANObject<TInputImage, TRealType>::VectorType vec,
                          typename TInputImage::Pointer mask )
{
  if( mask.IsNull() )
    {
    return vec;
    }
  ImagePointer image = this->ConvertVariateToSpatialImage( vec, mask, false );

  typedef itk::LaplacianRecursiveGaussianImageFilter<ImageType, ImageType> dgf;
  typename dgf::Pointer filter = dgf::New();
  RealType spacingsize = 0;
  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    RealType sp = mask->GetSpacing()[d];
    spacingsize += sp * sp;
    }
  spacingsize = sqrt( spacingsize );
  filter->SetSigma( 0.333 * spacingsize );
  filter->SetInput(image);
  filter->Update();
  image = filter->GetOutput();
  VectorType gradvec = this->ConvertImageToVariate( image,  this->m_MaskImageP );
  return gradvec;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::ComputeVectorGradMag( typename antsSCCANObject<TInputImage, TRealType>::VectorType vec,
                        typename TInputImage::Pointer mask )
{
  ImagePointer image = this->ConvertVariateToSpatialImage( vec, mask, false );

  typedef itk::GradientMagnitudeRecursiveGaussianImageFilter<ImageType, ImageType> dgf;
  typename dgf::Pointer filter = dgf::New();
  RealType spacingsize = 0;
  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    RealType sp = mask->GetSpacing()[d];
    spacingsize += sp * sp;
    }
  spacingsize = sqrt( spacingsize );
  filter->SetSigma( 0.333 * spacingsize );
  filter->SetInput(image);
  filter->Update();
  image = filter->GetOutput();
  VectorType gradvec = this->ConvertImageToVariate( image,  this->m_MaskImageP );
  return gradvec;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::MatrixType
antsSCCANObject<TInputImage, TRealType>
::NormalizeMatrix( typename antsSCCANObject<TInputImage, TRealType>::MatrixType p, bool makepositive )
{
  MatrixType np( p.rows(), p.columns() );

  for( unsigned long i = 0; i < p.columns(); i++ )
    {
    VectorType wpcol = p.get_column(i);
    VectorType wpcol2 = wpcol - wpcol.mean();
    double     sd = wpcol2.squared_magnitude();
    sd = sqrt( sd / (p.rows() - 1) );
    if( sd <= 0.0 && i == static_cast<unsigned long>(0) )
      {
      ::ants::antscout << " row " << i << " has zero variance --- exiting " << std::endl;
      std::exception();
      }
    if( sd <= 0 && i > 0 )
      {
      ::ants::antscout << " row " << i << " has zero variance --- copying the previous row " << std::endl;
      ::ants::antscout << " the row is " << wpcol << std::endl;
      np.set_column(i, np.get_column(i - 1) );
      }
    else
      {
      wpcol = wpcol2 / sd;
      np.set_column(i, wpcol);
      }
    }
  /** cast to a non-negative space */
  if( makepositive )
    {
    np = np - np.min_value();
    }
  return np;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::MatrixType
antsSCCANObject<TInputImage, TRealType>
::VNLPseudoInverse( typename antsSCCANObject<TInputImage, TRealType>::MatrixType rin, bool take_sqrt )
{
  double       pinvTolerance = this->m_PinvTolerance;
  MatrixType   dd = rin;
  unsigned int ss = dd.rows();

  if( dd.rows() > dd.columns() )
    {
    ss = dd.columns();
    }
  vnl_svd<RealType> eig(dd, pinvTolerance);
  if( !take_sqrt )
    {
    return eig.pinverse( ss );
    }
  else
    {
    for( unsigned int j = 0; j < ss; j++ )
      {
      RealType eval = eig.W(j, j);
      if( eval > pinvTolerance )
        {                         // FIXME -- check tolerances against matlab pinv
        eig.W(j, j) = 1 / (eval); // need eval for inv cov
        if( take_sqrt )
          {
          eig.W(j, j) = 1 / sqrt(eval);
          }
        }
      else
        {
        eig.W(j, j) = 0;
        }
      }
    /** there is a scaling problem with the pseudoinverse --- this is a cheap fix!!
            it is based on the theoretical frobenious norm of the inverse matrix */
    MatrixType pinv = ( eig.recompose() ).transpose();
    double     a = sqrt( (double)dd.rows() );
    double     b = (pinv * dd).frobenius_norm();
    pinv = pinv * a / b;
    return pinv;
    }
}



template <class TInputImage, class TRealType>
void
antsSCCANObject<TInputImage, TRealType>
::SoftClustThreshold( typename antsSCCANObject<TInputImage, TRealType>::VectorType &  v_in,
  TRealType soft_thresh, bool keep_positive , unsigned int clust, typename TInputImage::Pointer mask )
{
  // here , we apply the minimum threshold to the data.
  for( unsigned int i = 0; i < v_in.size(); i++ )
    {
    RealType val = v_in(i);
    if ( keep_positive && val < 0 ) val = 0 ;
    // if ( keep_positive && val < 0 ) val *= ( -1 );
    if( vnl_math_abs( val ) < soft_thresh )
      {
      v_in(i) = 0;
      }
    else
      {
      v_in(i) = val;
      if( this->m_UseL1 )
        {
	if ( v_in(i) > 0 ) v_in(i) = v_in(i) - soft_thresh;
	if ( v_in(i) < 0 ) v_in(i) = v_in(i) + soft_thresh;
        }
      }
    }
  this->ClusterThresholdVariate( v_in, mask, clust );
  //  ::ants::antscout <<" resoft " << this->CountNonZero( v_in ) << std::endl;
  return;
}



template <class TInputImage, class TRealType>
void
antsSCCANObject<TInputImage, TRealType>
::ReSoftThreshold( typename antsSCCANObject<TInputImage, TRealType>::VectorType &
                   v_in, TRealType fractional_goal, bool keep_positive )
{
  //  ::ants::antscout <<" resoft " << fractional_goal << std::endl;
  if( fabs(fractional_goal) >= 1 || fabs( (float)(v_in.size() ) * fractional_goal) <= 1 )
    {
    return;
    }
  RealType maxv = v_in.max_value();
  if( fabs(v_in.min_value() ) > maxv )
    {
    maxv = fabs(v_in.min_value() );
    }
  RealType     lambg = this->m_Epsilon;
  RealType     frac = 0;
  unsigned int its = 0, ct = 0;
  RealType     soft_thresh = lambg;
  for( unsigned int i = 0; i < v_in.size(); i++ )
    {
    if(  keep_positive && v_in(i) < 0 )
      {
      v_in(i) = 0;
      }
    }

  RealType     minthresh = 0, minfdiff = 1;
  unsigned int maxits = 1000;
  for( its = 0; its < maxits; its++ )
    {
    soft_thresh = (its / (float)maxits) * maxv;
    ct = 0;
    for( unsigned int i = 0; i < v_in.size(); i++ )
      {
      RealType val = v_in(i);
      if( !keep_positive )
        {
        val = fabs(val);
        }
      else if( val < 0 )
        {
        val = 0;
        }
      if( val < soft_thresh )
        {
        ct++;
        }
      }
    frac = (float)(v_in.size() - ct) / (float)v_in.size();
    //    ::ants::antscout << " cur " << frac << " goal "  << fractional_goal << " st " << soft_thresh << " th " <<
    // minthresh
    // << std::endl;
    if( fabs(frac - fractional_goal) < minfdiff )
      {
      minthresh = soft_thresh;
      minfdiff = fabs(frac - fractional_goal);
      }
    }
  //  ::ants::antscout << " goal "  << fractional_goal << " st " << soft_thresh << " th " << minthresh << " minfdiff "
  // <<
  // minfdiff << std::endl;

// here , we apply the minimum threshold to the data.
  ct = 0;
  for( unsigned int i = 0; i < v_in.size(); i++ )
    {
    RealType val = v_in(i);
    if( !keep_positive )
      {
      val = fabs(val);
      }
    else if( val < 0 )
      {
      val = 0;
      }
    if( val < minthresh )
      {
      v_in(i) = 0;
      ct++;
      }
    else
      {
      if( this->m_UseL1 )
        {
        v_in(i) = v_in(i) - minthresh;
        }
      }
    }
  //  ::ants::antscout << " post minv " << tminv << " post maxv " << tmaxv <<  std::endl;
  frac = (float)(v_in.size() - ct) / (float)v_in.size();
  // ::ants::antscout << " frac non-zero " << frac << " wanted " << fractional_goal << std::endl;
  //  if ( v_in.two_norm() > this->m_Epsilon ) v_in=v_in/v_in.two_norm();
  //  ::ants::antscout << v_in <<std::endl;
  return;
}

template <class TInputImage, class TRealType>
void
antsSCCANObject<TInputImage, TRealType>
::ConstantProbabilityThreshold( typename antsSCCANObject<TInputImage, TRealType>::VectorType &
                                v_in, TRealType probability_goal, bool keep_positive )
{
  bool debug = false;

  v_in = v_in / v_in.two_norm();
  VectorType v_out(v_in);
  RealType   maxv = v_in.max_value();
  if( fabs(v_in.min_value() ) > maxv )
    {
    maxv = fabs(v_in.min_value() );
    }
  RealType     lambg = this->m_Epsilon;
  RealType     frac = 0;
  unsigned int its = 0;
  RealType     probability = 0;
  RealType     probability_sum = 0;
  RealType     soft_thresh = lambg;
  unsigned int nzct = 0;
  for( unsigned int i = 0; i < v_in.size(); i++ )
    {
    if( keep_positive && v_in(i) < 0 )
      {
      v_in(i) = 0;
      }
    v_out(i) = v_in(i);
    probability_sum += fabs(v_out(i) );
    if( fabs( v_in( i ) ) > 0 )
      {
      nzct++;
      }
    }
  if( debug )
    {
    ::ants::antscout << " prob sum " << probability_sum << std::endl;
    }
  if( debug )
    {
    ::ants::antscout << " nzct " << nzct << std::endl;
    }
  RealType     minthresh = 0, minfdiff = 1;
  unsigned int maxits = 1000;
  for( its = 0; its < maxits; its++ )
    {
    soft_thresh = (its / (float)maxits) * maxv;
    probability = 0;
    for( unsigned int i = 0; i < v_in.size(); i++ )
      {
      RealType val = v_in(i);
      if( !keep_positive )
        {
        val = fabs(val);
        }
      else if( val < 0 )
        {
        val = 0;
        }
      if( val < soft_thresh )
        {
        v_out(i) = 0;
        }
      else
        {
        v_out(i) = v_in(i);
        probability += fabs(v_out(i) );
        }
      }
    probability /= probability_sum;
    if( debug )
      {
      ::ants::antscout << " cur " << probability << " goal "  << probability_goal << " st " << soft_thresh << " th "
                       << minthresh << std::endl;
      }
    if( fabs(probability - probability_goal) < minfdiff )
      {
      minthresh = soft_thresh;
      minfdiff = fabs(probability - probability_goal);
      }
    }

// here , we apply the minimum threshold to the data.
  probability = 0;
  unsigned long ct = 0;
  for( unsigned int i = 0; i < v_in.size(); i++ )
    {
    RealType val = v_in(i);
    if( !keep_positive )
      {
      val = fabs(val);
      }
    else if( val < 0 )
      {
      val = 0;
      }
    if( val < minthresh )
      {
      v_in(i) = 0;
      ct++;
      }
    else
      {
      // v_in(i)-=minthresh;
      probability += fabs(v_in(i) );
      }
    }
  if( debug )
    {
    ::ants::antscout << " frac non-zero " << probability << " wanted " <<  probability_goal << " Keep+ "
                     << keep_positive
                     << std::endl;
    }
  frac = (float)(v_in.size() - ct) / (float)v_in.size();
  if( frac < 1 )
    {
    ::ants::antscout << " const prob " << probability / probability_sum << " sparseness " << frac << std::endl;
    }
  //  if ( v_in.two_norm() > this->m_Epsilon ) v_in=v_in/v_in.sum();

  return;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::VectorType
antsSCCANObject<TInputImage, TRealType>
::TrueCCAPowerUpdate( TRealType penalty1,
                      typename antsSCCANObject<TInputImage,
                                               TRealType>::MatrixType p,
                      typename antsSCCANObject<TInputImage,
                                               TRealType>::VectorType  w_q,
                      typename antsSCCANObject<TInputImage, TRealType>::MatrixType q, bool keep_pos,
                      bool factorOutR )
{
  RealType norm = 0;
  // recall that the matrices below have already be whitened ....
  // we bracket the computation and use associativity to make sure its done efficiently
  // vVector wpnew=( (CppInv.transpose()*p.transpose())*(CqqInv*q) )*w_q;
  VectorType wpnew;

  if( factorOutR )
    {
    VectorType temp = q * w_q;
    wpnew = p.transpose() * ( temp - this->m_MatrixRRt * temp );
    }
  else
    {
    VectorType temp = q * w_q;
    wpnew = p.transpose() * temp;
    }
  this->ReSoftThreshold( wpnew, penalty1, keep_pos );
  norm = wpnew.two_norm();
  if( norm > this->m_Epsilon )
    {
    wpnew = wpnew / (norm);
    }
  return wpnew;
}

template <class TInputImage, class TRealType>
void
antsSCCANObject<TInputImage, TRealType>
::UpdatePandQbyR()
{
// R is already whitened
  switch( this->m_SCCANFormulation )
    {
    case PQ:
      {
// do nothing
      }
      break;
    case PminusRQ:
      {
      this->m_MatrixP = (this->m_MatrixP - this->m_MatrixRRt * this->m_MatrixP);
      }
      break;
    case PQminusR:
      {
      this->m_MatrixQ = (this->m_MatrixQ - this->m_MatrixRRt * this->m_MatrixQ);
      }
      break;
    case PminusRQminusR:
      {
/** P_R =   P - R_w R_w^T P */
/** Q_R =   Q - R_w R_w^T Q */
      this->m_MatrixP = (this->m_MatrixP - this->m_MatrixRRt * this->m_MatrixP);
      this->m_MatrixQ = (this->m_MatrixQ - this->m_MatrixRRt * this->m_MatrixQ);
      }
      break;
    case PQR:
      {
      ::ants::antscout << " You should call mscca not pscca " << std::endl;
      }
      break;
    }
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::RunDiagnostics( unsigned int n_vecs )
{
  ::ants::antscout << "Quantitative diagnostics: " << std::endl;
  ::ants::antscout << "Type 1: correlation from canonical variate to confounding vector " << std::endl;
  ::ants::antscout << "Type 2: correlation from canonical variate to canonical variate " << std::endl;
  RealType   corrthresh = 0.3;
  MatrixType omatP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  MatrixType omatQ;
  bool       doq = true;

  if( this->m_OriginalMatrixQ.size() > 0 )
    {
    omatQ = this->NormalizeMatrix(this->m_OriginalMatrixQ);
    }
  else
    {
    doq = false; omatQ = omatP;
    }
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    for( unsigned int wv = 0; wv < n_vecs; wv++ )
      {
      for( unsigned int col = 0; col < this->m_MatrixR.columns(); col++ )
        {
        RealType a =
          this->PearsonCorr(omatP * this->m_VariatesP.get_column(wv), this->m_OriginalMatrixR.get_column(col) );
        RealType b =
          this->PearsonCorr(omatQ * this->m_VariatesQ.get_column(wv), this->m_OriginalMatrixR.get_column(col) );
//      ::ants::antscout << "Pvec " << wv << " confound " << col << " : " << a <<std::endl;
//      ::ants::antscout << "Qvec " << wv << " confound " << col << " : " << b <<std::endl;
        if( fabs(a) > corrthresh && fabs(b) > corrthresh )
          {
          ::ants::antscout << " correlation with confound too high for variate " << wv << " corrs " << a << " and "
                           << b
                           <<  std::endl;
          //     this->m_CanonicalCorrelations[wv]=0;
          }
        }
      }
    }
  for( unsigned int wv = 0; wv < n_vecs; wv++ )
    {
    for( unsigned int yv = wv + 1; yv < n_vecs; yv++ )
      {
      RealType a =
        this->PearsonCorr(omatP * this->m_VariatesP.get_column(wv), omatP * this->m_VariatesP.get_column(yv) );
      if( fabs(a) > corrthresh )
        {
        ::ants::antscout << " not orthogonal p " <<  a << std::endl;
        //       this->m_CanonicalCorrelations[yv]=0;
        }
      ::ants::antscout << "Pvec " << wv << " Pvec " << yv << " : " << a << std::endl;
      if( doq )
        {
        RealType b =
          this->PearsonCorr(omatQ * this->m_VariatesQ.get_column(wv), omatQ * this->m_VariatesQ.get_column(yv) );
        if( fabs(b) > corrthresh )
          {
          ::ants::antscout << " not orthogonal q " <<  a << std::endl;
          //     this->m_CanonicalCorrelations[yv]=0;
          }
        ::ants::antscout << "Qvec " << wv << " Qvec " << yv << " : " << b << std::endl;
        } // doq
      }
    }
}

struct my_sccan_sort_class
  {
  bool operator()(double i, double j)
  {
    return i > j;
  }
  } my_sccan_sort_object;

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseCCA(unsigned int /* nvecs */)
{
  ::ants::antscout << " ed sparse cca " << std::endl;
  unsigned int nsubj = this->m_MatrixP.rows();

  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->NormalizeMatrix(this->m_OriginalMatrixQ);

  RealType tau = 0.01;
  if( this->m_Debug )
    {
    ::ants::antscout << " inv view mats " << std::endl;
    }
  MatrixType inviewcovmatP =
    ( (this->m_MatrixP
       * this->m_MatrixP.transpose() )
      * (this->m_MatrixP
         * this->m_MatrixP.transpose() ) )
    * (1 - tau) +  ( this->m_MatrixP * this->m_MatrixP.transpose() ) * tau * (RealType)nsubj;
  MatrixType inviewcovmatQ =
    ( (this->m_MatrixQ
       * this->m_MatrixQ.transpose() )
      * (this->m_MatrixQ
         * this->m_MatrixQ.transpose() ) )
    * (1 - tau) + ( this->m_MatrixQ * this->m_MatrixQ.transpose() ) * tau * (RealType)nsubj;

/** standard cca */
//  MatrixType CppInv=this->PseudoInverseCovMat(this->m_MatrixP);
//  MatrixType CqqInv=this->PseudoInverseCovMat(this->m_MatrixQ);
//  MatrixType cov=(CppInv*this->m_MatrixP).transpose()*(CqqInv*this->m_MatrixQ);
//  MatrixType
// TT=(this->m_MatrixP.transpose()*this->m_MatrixQ)*(this->m_MatrixP.transpose()*this->m_MatrixQ).transpose();
//  MatrixType TT=( (CppInv*this->m_MatrixP).transpose()*(CqqInv*this->m_MatrixQ))*
//                         (this->m_MatrixP.transpose()*this->m_MatrixQ).transpose();

/** dual cca */
  MatrixType CppInv = this->PseudoInverse(inviewcovmatP);
  MatrixType CqqInv = this->PseudoInverse(inviewcovmatQ);

  /** need the eigenvectors of this reduced matrix */
  //  eMatrix pccain=CppInv*Cpq*CqqInv*Cqp;
  MatrixType ccap = ( (CppInv * (this->m_MatrixP * this->m_MatrixP.transpose() ) ).transpose()
                      * (CqqInv * (this->m_MatrixQ * this->m_MatrixQ.transpose() ) )
                      * ( (this->m_MatrixP * this->m_MatrixP.transpose() ).transpose()
                          * (this->m_MatrixQ * this->m_MatrixQ.transpose() ) ).transpose() );
// convert to eigen3 format
/*
  eMatrix pccain=this->mVtoE(ccap);

  typedef Eigen::EigenSolver<eMatrix> eigsolver;
  eigsolver pEG( pccain );
//  eigsolver qEG( qccain );
  eMatrix pccaVecs = pEG.pseudoEigenvectors();
  eMatrix pccaSquaredCorrs=pEG.pseudoEigenvalueMatrix();

// call this function to check we are doing conversions correctly , matrix-wise
  this->mEtoV(pccaVecs);
// map the variates back to P, Q space and sort them
  this->m_CanonicalCorrelations.set_size(nvecs);
  this->m_CanonicalCorrelations.fill(0);
// copy to stl vector so we can sort the results
  MatrixType projToQ=( CqqInv*( (this->m_MatrixQ*this->m_MatrixQ.transpose() )* (this->m_MatrixP*this->m_MatrixP.transpose() ) ));
  std::vector<TRealType> evals(pccaSquaredCorrs.cols(),0);
  std::vector<TRealType> oevals(pccaSquaredCorrs.cols(),0);
  for ( long j=0; j<pccaSquaredCorrs.cols(); ++j){
    RealType val=pccaSquaredCorrs(j,j);
    if ( val > 0.05 ){
      VectorType temp=this->vEtoV( pccaVecs.col(  j ) );
      VectorType tempq=projToQ*temp;
      VectorType pvar=temp*this->m_MatrixP;
      this->ReSoftThreshold( pvar , this->m_FractionNonZeroP , this->m_KeepPositiveP );
      VectorType qvar= tempq*this->m_MatrixQ;
      this->ReSoftThreshold( qvar , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
      evals[j]=fabs(this->PearsonCorr(this->m_MatrixP*pvar,this->m_MatrixQ*qvar));
      oevals[j]=evals[j];
    }
  }

// sort and reindex the eigenvectors/values
  sort (evals.begin(), evals.end(), my_sccan_sort_object);
  std::vector<int> sorted_indices(nvecs,-1);
  for (unsigned int i=0; i<evals.size(); i++) {
  for (unsigned int j=0; j<evals.size(); j++) {
    if ( evals[i] == oevals[j] &&  sorted_indices[i] == -1 ) {
      sorted_indices[i]=j;
      oevals[j]=0;
    }
  }}

  this->m_VariatesP.set_size(this->m_MatrixP.cols(),nvecs);
  this->m_VariatesQ.set_size(this->m_MatrixQ.cols(),nvecs);
  for (unsigned int i=0; i<nvecs; i++) {
    VectorType temp=this->vEtoV( pccaVecs.col(  sorted_indices[i] ) );
    VectorType tempq=projToQ*temp;
    VectorType pvar= temp*this->m_MatrixP;
    this->ReSoftThreshold(pvar , this->m_FractionNonZeroP , this->m_KeepPositiveP );
    VectorType qvar= tempq*this->m_MatrixQ ;
    this->ReSoftThreshold(qvar , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
    this->m_VariatesP.set_column( i, pvar  );
    this->m_VariatesQ.set_column( i, qvar  );
  }

  for (unsigned int i=0; i<nvecs; i++) {
    this->m_CanonicalCorrelations[i]=
      this->PearsonCorr(this->m_MatrixP*this->GetVariateP(i),this->m_MatrixQ*this->GetVariateQ(i) );
    ::ants::antscout << "correlation of mapped back data " << this->m_CanonicalCorrelations[i] <<
     " eval " << pccaSquaredCorrs(sorted_indices[i],sorted_indices[i]) << std::endl;
  }
  for (unsigned int i=0; i<nvecs; i++) {
    ::ants::antscout << "inner prod of projections 0 vs i  " <<  this->PearsonCorr( this->m_MatrixP*this->GetVariateP(0) , this->m_MatrixP*this->GetVariateP(i) ) << std::endl;
  }
*/
//  this->RunDiagnostics(nvecs);
  return this->m_CanonicalCorrelations[0];
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparsePartialCCA(unsigned int /* nvecs */)
{
  /*
  ::ants::antscout <<" ed sparse partial cca " << std::endl;
  unsigned int nsubj=this->m_MatrixP.rows();
  this->m_MatrixP=this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ=this->NormalizeMatrix(this->m_OriginalMatrixQ);
  this->m_MatrixR=this->NormalizeMatrix(this->m_OriginalMatrixR);

  RealType tau=0.01;
  if (this->m_Debug) ::ants::antscout << " inv view mats " << std::endl;
  this->m_MatrixRRt=this->ProjectionMatrix(this->m_OriginalMatrixR);
  MatrixType PslashR=this->m_MatrixP;
  if ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )
    PslashR=this->m_MatrixP-(this->m_MatrixRRt*this->m_MatrixP);
  MatrixType QslashR=this->m_MatrixQ;
  if ( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )
    QslashR=this->m_MatrixQ-this->m_MatrixRRt*this->m_MatrixQ;

  if (this->m_Debug) {
    ::ants::antscout <<" corr-pre " << this->PearsonCorr( this->m_MatrixP.get_column(0) , this->m_OriginalMatrixR.get_column(0)  ) << std::endl  ;ants::antscout <<" corr-post " << this->PearsonCorr( PslashR.get_column(0) , this->m_OriginalMatrixR.get_column(0)  ) << std::endl;
    ::ants::antscout <<" corr-pre " << this->PearsonCorr( this->m_MatrixQ.get_column(0) , this->m_OriginalMatrixR.get_column(0)  ) << std::endl  ;ants::antscout <<" corr-post " << this->PearsonCorr( QslashR.get_column(0) , this->m_OriginalMatrixR.get_column(0)  ) << std::endl;
  }
  MatrixType inviewcovmatP=( (PslashR*PslashR.transpose())*(PslashR*PslashR.transpose()) )*(1-tau)+  ( PslashR*PslashR.transpose() )*tau*(RealType)nsubj;
  MatrixType inviewcovmatQ=( (QslashR*QslashR.transpose())*(QslashR*QslashR.transpose()) )*(1-tau)+( QslashR*QslashR.transpose() )*tau*(RealType)nsubj;

// dual cca
  if (this->m_Debug) ::ants::antscout << " inv view mats pinv " << std::endl;
  MatrixType CppInv=this->PseudoInverse(inviewcovmatP);
  MatrixType CqqInv=this->PseudoInverse(inviewcovmatQ);
  if (this->m_Debug) ::ants::antscout << " inv view mats pinv done " << std::endl;

  // need the eigenvectors of this reduced matrix
  MatrixType ccap=( (CppInv*(PslashR*PslashR.transpose())).transpose() *
                  (CqqInv*(QslashR*QslashR.transpose()))*
                        ( (PslashR*PslashR.transpose() ).transpose()*
                          (QslashR*QslashR.transpose() ) ).transpose() );
// convert to eigen3 format
  eMatrix pccain=this->mVtoE(ccap);

  typedef Eigen::EigenSolver<eMatrix> eigsolver;
  eigsolver pEG( pccain );
  eMatrix pccaVecs = pEG.pseudoEigenvectors();
  eMatrix pccaSquaredCorrs=pEG.pseudoEigenvalueMatrix();
// call this function to check we are doing conversions correctly , matrix-wise
  this->mEtoV(pccaVecs);
// map the variates back to P, Q space and sort them
  this->m_CanonicalCorrelations.set_size(nvecs);
  this->m_CanonicalCorrelations.fill(0);
  MatrixType projToQ=( CqqInv*( (QslashR*QslashR.transpose() )* (PslashR*PslashR.transpose() ) ));
// copy to stl vector so we can sort the results
  std::vector<TRealType> evals(pccaSquaredCorrs.cols(),0);
  std::vector<TRealType> oevals(pccaSquaredCorrs.cols(),0);
  for ( long j=0; j<pccaSquaredCorrs.cols(); ++j){
    RealType val=pccaSquaredCorrs(j,j);
    if ( val > 0.05 ){
      VectorType temp=this->vEtoV( pccaVecs.col(  j ) );
      VectorType tempq=projToQ*temp;
      VectorType pvar= temp*PslashR;
      this->ReSoftThreshold(pvar  , this->m_FractionNonZeroP , this->m_KeepPositiveP );
      VectorType qvar=tempq*QslashR;
      this->ReSoftThreshold(qvar , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
      evals[j]=fabs(this->PearsonCorr(PslashR*pvar,QslashR*qvar));
      oevals[j]=evals[j];
    }
  }

// sort and reindex the eigenvectors/values
  sort (evals.begin(), evals.end(), my_sccan_sort_object);
  std::vector<int> sorted_indices(nvecs,-1);
  for (unsigned int i=0; i<evals.size(); i++) {
  for (unsigned int j=0; j<evals.size(); j++) {
    if ( evals[i] == oevals[j] &&  sorted_indices[i] == -1 ) {
      sorted_indices[i]=j;
      oevals[j]=0;
    }
  }}

  this->m_VariatesP.set_size(PslashR.cols(),nvecs);
  this->m_VariatesQ.set_size(QslashR.cols(),nvecs);
  for (unsigned int i=0; i<nvecs; i++) {
    VectorType temp=this->vEtoV( pccaVecs.col(  sorted_indices[i] ) );
    VectorType tempq=projToQ*temp;
    VectorType pvar=temp*PslashR ;
    this->ReSoftThreshold(pvar  , this->m_FractionNonZeroP , this->m_KeepPositiveP );
    VectorType qvar=tempq*QslashR;
    this->ReSoftThreshold(qvar  , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
    this->m_VariatesP.set_column( i, pvar  );
    this->m_VariatesQ.set_column( i, qvar  );
  }

  for (unsigned int i=0; i<nvecs; i++) {
    this->m_CanonicalCorrelations[i]=
      this->PearsonCorr(PslashR*this->GetVariateP(i),QslashR*this->GetVariateQ(i) );
    ::ants::antscout << "correlation of mapped back data " << this->m_CanonicalCorrelations[i] <<  " eval " << pccaSquaredCorrs(sorted_indices[i],sorted_indices[i]) << std::endl;
  }
  for (unsigned int i=0; i<nvecs; i++) {
    ::ants::antscout << "inner prod of projections 0 vs i  " <<  this->PearsonCorr( PslashR*this->GetVariateP(0) ,  PslashR*this->GetVariateP(i) ) << std::endl;
  }

  this->RunDiagnostics(nvecs);
  */
  return this->m_CanonicalCorrelations[0];
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::SortResults(unsigned int n_vecs)
{
  bool debug = false;

// sort and reindex the eigenvectors/values
  std::vector<TRealType> evals(n_vecs, 0);
  std::vector<TRealType> oevals(n_vecs, 0);

  if( debug )
    {
    ::ants::antscout << "sort-a" << std::endl;
    }
  for( unsigned long j = 0; j < n_vecs; ++j )
    {
    RealType val = fabs(this->m_CanonicalCorrelations[j]);
    evals[j] = val;
    oevals[j] = val;
    }
  if( debug )
    {
    ::ants::antscout << "sort-b" << std::endl;
    }
  sort(evals.begin(), evals.end(), my_sccan_sort_object);
  std::vector<int> sorted_indices( n_vecs, n_vecs - 1 );
  for( unsigned int i = 0; i < evals.size(); i++ )
    {
    for( unsigned int j = 0; j < evals.size(); j++ )
      {
      if( fabs( evals[i] - oevals[j] ) < 1.e-9 &&
          sorted_indices[i] == static_cast<int>( n_vecs - 1 ) )
        {
        sorted_indices[i] = j;
        oevals[j] = 0;
        }
      }
    }
  if( debug )
    {
    for( unsigned int i = 0; i < evals.size(); i++ )
      {
      ::ants::antscout << " sorted " << i << " is " << sorted_indices[i] << " ev " << evals[i] << " oev "
                       << oevals[i] << std::endl;
      }
    }
  if( debug )
    {
    ::ants::antscout << "sort-c" << std::endl;
    }
  VectorType newcorrs(n_vecs, 0);
  MatrixType varp( this->m_VariatesP.rows(), this->m_VariatesP.columns(), 0);
  MatrixType varq( this->m_VariatesQ.rows(), this->m_VariatesQ.columns(), 0);
  if( debug )
    {
    ::ants::antscout << "sort-d" << std::endl;
    }
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    // if ( sorted_indices[i] > 0 ) {
    varp.set_column(i, this->m_VariatesP.get_column( sorted_indices[i] ) );
    if( varq.columns() > i )
      {
      varq.set_column(i, this->m_VariatesQ.get_column( sorted_indices[i] ) );
      }
    newcorrs[i] = (this->m_CanonicalCorrelations[sorted_indices[i]]);
    // }
    }
  if( debug )
    {
    ::ants::antscout << "sort-e" << std::endl;
    }
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    if( debug )
      {
      ::ants::antscout << "sort-e-a" << std::endl;
      }
    this->m_VariatesP.set_column(i, varp.get_column( i ) );
    if( debug )
      {
      ::ants::antscout << "sort-e-b" << std::endl;
      }
    if(  this->m_VariatesQ.columns() > i )
      {
      if( debug )
        {
        ::ants::antscout << "sort-e-c" << i << " varqcol " << varq.columns() << " VarQcol "
                         << this->m_VariatesQ.columns()  <<  std::endl;
        }
      this->m_VariatesQ.set_column( i, varq.get_column( i ) );
      }
    }
  if( debug )
    {
    ::ants::antscout << "sort-f" << std::endl;
    }
  this->m_CanonicalCorrelations = newcorrs;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseArnoldiSVD_z(unsigned int n_vecs )
{
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  ::ants::antscout << " arnoldi sparse svd : cg " << std::endl;
  std::vector<RealType> vexlist;
  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
    }
  this->m_ClusterSizes.set_size(n_vecs);
  this->m_ClusterSizes.fill(0);
  double trace = 0;
  for( unsigned int i = 0; i < this->m_MatrixP.cols(); i++ )
    {
    trace += inner_product(this->m_MatrixP.get_column(i), this->m_MatrixP.get_column(i) );
    }
  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  VariateType myGradients;
  this->m_SparseVariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_SparseVariatesP.fill(0);
  myGradients.set_size(this->m_MatrixP.cols(), n_vecs);
  myGradients.fill(0);
  MatrixType init = this->GetCovMatEigenvectors( this->m_MatrixP );
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    this->m_VariatesP.set_column(kk, this->InitializeV(this->m_MatrixP) );
    if( kk < init.columns() )
      {
      VectorType initv = init.get_column(kk) * this->m_MatrixP;
      this->m_VariatesP.set_column(kk, initv);
      }
    }
  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
// Arnoldi Iteration SVD/SPCA
  unsigned int loop = 0;
  bool         debug = false;
  double       convcrit = 1;
  RealType     fnp = 1;
  while( loop<maxloop && convcrit> 1.e-8 )
    {
    fnp = this->m_FractionNonZeroP;
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType ptemp = this->m_SparseVariatesP.get_column(k);
      // if ( loop == 0 )
      ptemp = this->m_VariatesP.get_column(k);
      MatrixType pmod = this->m_MatrixP;
      VectorType pveck = pmod.transpose() * ( pmod  * ptemp ); // classic
      RealType   hkkm1 = pveck.two_norm();
      if( hkkm1 > this->m_Epsilon  )
        {
        pveck = pveck / hkkm1;
        }
      for( unsigned int j = 0; j < k; j++ )       //   \forall j \ne i x_j \perp x_i
        {
        VectorType qj = this->m_VariatesP.get_column(j);
        RealType   ip = inner_product(qj, qj);
        if( ip < this->m_Epsilon )
          {
          ip = 1;
          }
        RealType hjk = inner_product(qj, pveck) / ip;
        pveck = pveck - qj * hjk;
        }
      VectorType pvecknew(pveck);
      //  x_i is sparse
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( pvecknew, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( pvecknew, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( pvecknew, this->m_MaskImageP, this->m_MinClusterSizeP );
        pvecknew = pvecknew / pvecknew.two_norm();
        }
      /** this is the difference between the updated & orthogonalized (not sparse) pveck and the sparse pveck */
      /** put another way - minimize the difference between the sparse solution and the pca solution */
      // myGradients.set_column(k,pvecknew*this->m_CanonicalCorrelations[k]-pveck);
      myGradients.set_column(k, pvecknew - pveck / pveck.two_norm() );
      // residual between sparse and pca solution
      if( loop == 0 )
        {
        this->m_VariatesP.set_column(k, pvecknew);
        }
      } // kloop

    /** Now update the solution by this gradient */
    double recuravg = 0.5; // 0.99; //1/(double)(loop+1);
    double recuravg2 = 1 - recuravg;
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType proj = this->m_MatrixP * this->m_VariatesP.get_column( k );
      double     denom =  inner_product( proj, proj );  if( denom < this->m_Epsilon )
        {
        denom = 1.e9;
        }
      VectorType pveck  = myGradients.get_column(k);
      VectorType newsol = this->m_SparseVariatesP.get_column(k) * recuravg2 + pveck * recuravg;
      //    VectorType newsol = this->m_SparseVariatesP.get_column(k) - pveck * alphak;
      this->m_SparseVariatesP.set_column(k, newsol);
      }
    /** Project the solution space to the sub-space */
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType ptemp = this->m_SparseVariatesP.get_column(k);
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( ptemp, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( ptemp, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( ptemp, this->m_MaskImageP, this->m_MinClusterSizeP );
        ptemp = ptemp / ptemp.two_norm();
        }
      MatrixType pmod = this->m_MatrixP;
      VectorType pveck  = pmod.transpose() * ( pmod  * ptemp ); // classic
      RealType   hkkm1 = pveck.two_norm();
      if( hkkm1 > this->m_Epsilon  )
        {
        pveck = pveck / hkkm1;
        }
      for( unsigned int j = 0; j < k; j++ )
        {
        VectorType qj = this->m_VariatesP.get_column(j);
        RealType   ip = inner_product(qj, qj);
        if( ip < this->m_Epsilon )
          {
          ip = 1;
          }
        RealType hjk = inner_product(qj, pveck) / ip;
        pveck = pveck - qj * hjk;
        }
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( pveck, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( pveck, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( pveck, this->m_MaskImageP, this->m_MinClusterSizeP );
        pveck = pveck / pveck.two_norm();
        }
      this->m_VariatesP.set_column(k, pveck);
      } // kloop

    this->m_VariatesQ = this->m_VariatesP;
    if( debug )
      {
      ::ants::antscout << " get evecs " << std::endl;
      }
    RealType vex = this->ComputeSPCAEigenvalues(n_vecs, trace, true);
    vexlist.push_back(   vex    );
    this->SortResults( n_vecs );
    convcrit = ( this->ComputeEnergySlope(vexlist, 5) );
    ::ants::antscout << "Iteration: " << loop << " Eigenval_0: " << this->m_CanonicalCorrelations[0] << " Eigenval_N: "
                     << this->m_CanonicalCorrelations[n_vecs
                                     - 1] << " Sparseness: " << fnp  << " convergence-criterion: " << convcrit
                     <<  " vex " << vex << std::endl;
    loop++;
    if( debug )
      {
      ::ants::antscout << "wloopdone" << std::endl;
      }
    } // opt-loop
      //  ::ants::antscout << " cluster-sizes " << this->m_ClusterSizes << std::endl;
  for( unsigned int i = 0; i < vexlist.size(); i++ )
    {
    ::ants::antscout << vexlist[i] << ",";
    }
  ::ants::antscout << std::endl;
  return fabs(this->m_CanonicalCorrelations[0]);
}

template <class TInputImage, class TRealType>
TRealType
antsSCCANObject<TInputImage, TRealType>
::ReconstructionError( typename antsSCCANObject<TInputImage, TRealType>::MatrixType mymat,
                       typename antsSCCANObject<TInputImage, TRealType>::MatrixType myvariate )
{
  this->m_CanonicalCorrelations.set_size( myvariate.cols() );
  this->m_CanonicalCorrelations.fill( 0 );
  RealType     reconerr = 0;
  RealType     onenorm = 0;
  unsigned int n_vecs = myvariate.cols();
  RealType     clnum = ( RealType ) this->m_MatrixP.cols();
  for(  unsigned int a = 0; a < mymat.rows(); a++ )
    {
    VectorType x_i = mymat.get_row( a );
    onenorm += x_i.one_norm() / clnum;
    VectorType lmsolv( n_vecs, 1 );  // row for B
    (void) this->ConjGrad( myvariate, lmsolv, x_i, 0, 10000 );
    VectorType x_recon = ( this->m_VariatesP * lmsolv + this->m_Intercept );
    reconerr += ( x_i - x_recon ).one_norm() / clnum;
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      this->m_CanonicalCorrelations[i] = this->m_CanonicalCorrelations[i]  + fabs( lmsolv[i] );
      }
    }
  return ( onenorm - reconerr ) / onenorm;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseReconPrior(unsigned int n_vecs, bool prior)
{
  VectorType sparsenessparams( n_vecs, this->m_FractionNonZeroP );
  double     lambda = this->GetLambda(); // Make it a parameter

  //::ants::antscout << "lambda" << lambda  << " GradStep " << this->m_GradStep << std::endl;

  if( prior )
    {
   // ::ants::antscout << "prior " << this->m_OriginalMatrixPriorROI.rows() << " c "
    //                 << this->m_OriginalMatrixPriorROI.cols()  << std::endl;
    this->m_MatrixPriorROI = this->m_OriginalMatrixPriorROI;
    n_vecs = this->m_MatrixPriorROI.rows();
    for( unsigned int x = 0; x < this->m_OriginalMatrixPriorROI.rows(); x++ )
	{
      VectorType   priorrow = this->m_OriginalMatrixPriorROI.get_row( x );
      RealType fnz = 0;
      for( unsigned int y = 0; y < this->m_OriginalMatrixPriorROI.cols(); y++ )
	{
	if(vnl_math_abs( priorrow( y ) ) >1.e-10 )
          {
	  fnz += 1;
	  }
	}
      if( this->m_FractionNonZeroP <  1.e-10  )
	{
        sparsenessparams( x ) = (RealType) fnz / (RealType) this->m_OriginalMatrixPriorROI.cols() * 0.5;
	}
      priorrow = priorrow/priorrow.two_norm();
      this->m_MatrixPriorROI.set_row( x , priorrow );
      }
    //::ants::antscout <<"sparseness: " <<sparsenessparams << std::endl;
    }
	
  RealType reconerr = 0;
  this->m_CanonicalCorrelations.set_size( n_vecs );
  this->m_CanonicalCorrelations.fill( 0 );
  //::ants::antscout << " sparse recon prior " << this->m_MinClusterSizeP << std::endl;
  MatrixType matrixB( this->m_OriginalMatrixP.rows(), n_vecs );
  matrixB.fill( 0 );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );

	
	/*
	for( unsigned int x = 0; x < this->m_MatrixP.rows(); x++ )
	{
		VectorType   dataMatrixXrow = this->m_MatrixP.get_row( x );
		
		dataMatrixXrow=dataMatrixXrow/dataMatrixXrow.two_norm();
		this->m_MatrixP.set_row(x,dataMatrixXrow);
		
	}		*/
	
 
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), n_vecs );
  this->m_VariatesP.fill( 0 );
  VectorType icept( this->m_MatrixP.rows(), 0 );
 	
  if( prior )
    {
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      VectorType initvec = this->m_MatrixPriorROI.get_row(i);
      initvec = initvec / initvec.two_norm();
      this->SparsifyP( initvec );
		  
      this->m_VariatesP.set_column( i, initvec );
      }
    }
  else
    {
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      VectorType initvec = this->InitializeV( this->m_MatrixP, i + 1 );
      initvec = initvec / initvec.two_norm();
      this->m_VariatesP.set_column( i, initvec );
      }
    }

  /** now initialize B */
  reconerr = this->SparseReconB( matrixB, icept  );
  for( unsigned int overit = 0; overit < this->m_MaximumNumberOfIterations; overit++ )
    {
    // update V matrix
    /** a power iteration  method --- depends on the following

  given any nonzero $z \in \mathbb{R}^n$, the Rayleigh quotient
  $x^T X x / x^T x $ minimizes the function $\| \lambda x - X x \|^2 $
  wrt $\lambda$.

  so, if we find the vector x ( by sparse power iteration ) then we have a vector
  that is a close approximation to the first eigenvector of X. If X is a residual
  matrix then x is a good approximation of the $n^th$ eigenvector.

    **/		
    VectorType zero( this->m_MatrixP.cols(), 0 );
    VectorType zerob( this->m_MatrixP.rows(), 0 );
    for(  unsigned int a = 0; a < n_vecs; a++ )
      {
      this->m_FractionNonZeroP = sparsenessparams( a );
      VectorType bvec = matrixB.get_column( a );
      matrixB.set_column( a, zerob );  //  if  X =  U V^T  + Error   then   matrixB = U 
      MatrixType tempMatrix = this->m_VariatesP;
      tempMatrix.set_column( a, zero );
      MatrixType partialmatrix = matrixB * tempMatrix.transpose();
      for(  unsigned int interc = 0; interc < this->m_MatrixP.rows(); interc++ )
        {
        partialmatrix.set_row( interc, partialmatrix.get_row( interc ) + icept( interc ) );
        }
      partialmatrix = this->m_MatrixP - partialmatrix;
      VectorType priorVec = this->m_MatrixPriorROI.get_row(a);
      if( priorVec.one_norm()  < 1.e-12 )
	{
	::ants::antscout << "Prior Norm too small, could be a bug: " << priorVec.one_norm() << std::endl;
	  std::exception();
	}
      VectorType evec = this->m_VariatesP.get_column( a );
      priorVec = priorVec / priorVec.two_norm();
      this->m_CanonicalCorrelations[a] = this->IHTPowerIterationPrior(  partialmatrix,  evec, priorVec , 10, a, lambda );
      this->m_VariatesP.set_column( a, evec );
      matrixB.set_column( a, bvec );
      // /////////////////////
      // update B matrix by linear regression
      reconerr = this->SparseReconB( matrixB, icept  );	
      }
    ::ants::antscout << overit << ": %var " << reconerr << std::endl;
    }
  this->m_VariatesQ = matrixB;
  // this->SortResults( n_vecs );
  //  this->m_UseL1 = false;
  return 1.0 / reconerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::IHTPowerIterationPrior( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
                          typename antsSCCANObject<TInputImage, TRealType>::VectorType& evec,
                          typename antsSCCANObject<TInputImage, TRealType>::VectorType& priorin,
                          unsigned int maxits, unsigned int maxorth, double lambda )
{
  /*  
  // d/dv \| X_p - u_a v^t \| =  u_a^t ( X_p  - u_a v^t ) 
  //  u_a^t X_p -   u_a^t u_a v^t 
  // d/dv \| M - v v^t \| =  v^t ( M  - v v^t ) 
  //  v^t M -   ip( v, v ) v^t 
  VectorType bvec = this->m_MatrixU.get_column( maxorth );
  VectorType mygrad1 = ( bvec *  A - evec * inner_product( bvec, bvec ) ) * lam1;
  VectorType mygrad2 = ( this->FastOuterProductVectorMultiplication( priorin, evec ) 
			 - evec * inner_product( evec, evec ) ) * lam2;
  VectorType mygrad = mygrad1 + mygrad2 ;
  evec = evec +  mygrad * this->m_GradStep; 
  this->SparsifyP( evec );
  return 1;
  */
  /** This computes a hard-thresholded gradient descent on the eigenvector criterion.
      max x  over  x^t A^t A x s.t.  x^t x = 1
      success of the optimization is measured by rayleigh quotient. derivative is:
      d.dx ( x^t A^t A x  ) =   A^t A x  ,   x \leftarrow  x / \| x \|
      we use a conjugate gradient version of this optimization.
  */
  VectorType prior( priorin );
  if( evec.two_norm() ==  0 )
    {
    evec = this->InitializeV( this->m_MatrixP, false );
    }
  if( inner_product( prior, evec ) == 0 )
    {
    evec.update( prior, 0  );
    this->SparsifyP( evec  );
    }
  VectorType mgrad = this->FastOuterProductVectorMultiplication( prior, evec );
  VectorType proj = ( A * prior );
  VectorType lastgrad = evec;
  VectorType mlastgrad = mgrad;
  RealType   rayquo = 0, rayquold = -1;
  RealType   denom = inner_product( evec, evec );
  if( denom > 0 )
    {
    rayquo = inner_product( proj, proj  ) / denom;
    }
  MatrixType   At = A.transpose();
  unsigned int powerits = 0;
  VectorType   bestevec = evec;
  RealType     basevscale = 1;
  RealType     basepscale = 1;
  VectorType di;
  while( ( ( powerits < maxits ) && ( rayquo > rayquold ) ) || ( powerits < 4 ) ) 
    {
    VectorType pvec = this->FastOuterProductVectorMultiplication( prior , evec );
    VectorType nvec   = ( At   * ( A    * evec ) );
    if ( powerits == 0 )
      {
      basevscale = pvec.two_norm() / nvec.two_norm() * this->m_GradStep;
      basepscale = this->m_GradStep * lambda;
      }
    VectorType g1 = nvec * basevscale;
    VectorType g2 = pvec * basepscale;
    RealType gamma = 0.1;
    nvec = g1 + g2;
    if ( powerits == 0 ) di = nvec;
    if( ( lastgrad.two_norm() > 0  ) )
      {
      gamma = inner_product( nvec, nvec ) / inner_product( lastgrad, lastgrad );
      }
    lastgrad = nvec;
    VectorType diplus1 =  nvec + di * gamma ;
    evec = evec + diplus1;
    di = diplus1;
    for( unsigned int orth = 0; orth < maxorth; orth++ )
      {
      VectorType zv = this->m_VariatesP.get_column( orth );
      evec = this->Orthogonalize( evec, zv );
      if ( this->m_Covering ) this->ZeroProduct( evec,  zv );
      }
    this->SparsifyP( evec  );
    if( evec.two_norm() > 0 )
      {
      evec = evec / evec.two_norm();
      }
    rayquold = rayquo;
    denom = inner_product( evec, evec );
    if( denom > 0 )
      {
      // fast way to compute   evec^T * (  A^T A + M M^T ) evec
      proj =  ( At   * ( A    * evec ) ) * basevscale +
	( this->FastOuterProductVectorMultiplication( prior , evec ) ) * basepscale;
      rayquo = inner_product( evec, proj  ) / denom;
      }
    powerits++;
    if( rayquo > rayquold || powerits == 1 )
      {
      bestevec = evec;
      }
    }
  // ::ants::antscout << "rayleigh-quotient: " << rayquo << " in " << powerits << " num " << maxorth;
  evec = bestevec;
  if( inner_product( prior, evec ) == 0 )
    {
    evec.update( prior, 0  );
    this->SparsifyP( evec  );
    ::ants::antscout << " recompute " << maxorth << " new ip " << inner_product( prior, evec );
    }
  return rayquo;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseReconB(   typename antsSCCANObject<TInputImage,
                                           TRealType>::MatrixType& matrixB,
                  typename antsSCCANObject<TInputImage,
                                           TRealType>::VectorType& icept )
{
  MatrixType temp( this->m_MatrixP );
  icept.fill( 0 );
  RealType meancorr = 0;
  matrixB.set_size( this->m_MatrixP.rows(), this->m_VariatesP.cols() );
  matrixB.fill( 0 );
  for(  unsigned int a = 0; a < this->m_MatrixP.rows(); a++ )
    {
    VectorType x_i = this->m_MatrixP.get_row( a );
    VectorType lmsolv = matrixB.get_row( a );
    (void) this->ConjGrad(  this->m_VariatesP, lmsolv, x_i, 0, 100 ); // A x = b
    VectorType x_recon = ( this->m_VariatesP * lmsolv + this->m_Intercept );
    //    VectorType x_recon = ( this->m_VariatesP * lmsolv );
    icept( a ) = this->m_Intercept;
    matrixB.set_row( a, lmsolv );
    RealType localcorr = this->PearsonCorr( x_recon, x_i  );
    temp.set_row( a, x_recon );
    meancorr += localcorr;
    }
  if( false )
    {
    std::vector<std::string> ColumnHeaders;
    for( unsigned int nv = 0; nv < this->m_MatrixP.cols(); nv++ )
      {
      std::stringstream ss;
      ss << (nv + 1);
      std::string stringout = ss.str();
      std::string colname = std::string("X") + stringout;
      ColumnHeaders.push_back( colname );
      }
    MatrixType recon = ( matrixB * this->m_VariatesP.transpose() );
    typedef itk::CSVNumericObjectFileWriter<double, 1, 1> WriterType;
    WriterType::Pointer writer = WriterType::New();
    writer->SetFileName( "Zrecon.csv" );
    writer->SetInput( &recon );
    writer->Write();
    }
  this->m_MatrixU = matrixB;
  RealType matpfrobnorm = this->m_MatrixP.frobenius_norm();
  RealType rr = ( temp - this->m_MatrixP ).frobenius_norm();
  return ( 1.0 - rr * rr / ( matpfrobnorm * matpfrobnorm ) );
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseRecon(unsigned int n_vecs)
{
  RealType   reconerr = 0;
  VectorType sparsenessparams( n_vecs, this->m_FractionNonZeroP );

  this->m_CanonicalCorrelations.set_size( n_vecs );
  this->m_CanonicalCorrelations.fill( 0 );
  ::ants::antscout << " sparse recon : clust " << this->m_MinClusterSizeP << " UseL1 " << this->m_UseL1 <<  " Keep+ "
                   <<  this->m_KeepPositiveP << " covering " << this->m_Covering << " smooth " << this->m_Smoother << std::endl;
  MatrixType matrixB( this->m_OriginalMatrixP.rows(), n_vecs );
  matrixB.fill( 0 );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP  );
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), n_vecs );
  this->m_VariatesP.fill( 0 );
  VectorType icept( this->m_MatrixP.rows(), 0 );
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    VectorType initvec = this->InitializeV( this->m_MatrixP, i + 1 );
    for( unsigned int j = 0; j < i; j++ )
      {
      initvec = this->Orthogonalize( initvec, this->m_VariatesP.get_column( j ) );
      }
    initvec = this->SpatiallySmoothVector( initvec, this->m_MaskImageP );
    initvec = initvec / initvec.two_norm();
    this->m_VariatesP.set_column( i, initvec );
    }
  /** now initialize B */
  reconerr = this->SparseReconB( matrixB, icept  );
  ::ants::antscout << "begin : %var " << reconerr << std::endl;

  RealType matpfrobnorm = this->m_MatrixP.frobenius_norm();
  for( unsigned int overit = 0; overit < this->m_MaximumNumberOfIterations; overit++ )
    {
    // update V matrix
    /** a power iteration  method --- depends on the following

       given any nonzero $z \in \mathbb{R}^n$, the Rayleigh quotient
       $x^T X x / x^T x $ minimizes the function $\| \lambda x - X x \|^2 $
       wrt $\lambda$.

       so, if we find the vector x ( by sparse power iteration ) then we have a vector
       that is a close approximation to the first eigenvector of X. If X is a residual
       matrix then x is a good approximation of the $n^th$ eigenvector.

    **/
    VectorType   zero( this->m_MatrixP.cols(), 0 );
    VectorType   zerob( this->m_MatrixP.rows(), 0 );
    unsigned int a = 0;
    while(  a < n_vecs )
      {
      this->m_FractionNonZeroP = sparsenessparams( a );
      VectorType bvec = matrixB.get_column( a );
      matrixB.set_column( a, zerob );
      MatrixType tempMatrix = this->m_VariatesP;
      tempMatrix.set_column( a, zero );
      MatrixType partialmatrix = matrixB * tempMatrix.transpose();
      for(  unsigned int interc = 0; interc < this->m_MatrixP.rows(); interc++ )
        {
        partialmatrix.set_row( interc, partialmatrix.get_row( interc ) + icept( interc ) );
        }
      partialmatrix = this->m_MatrixP - partialmatrix;
      this->m_CanonicalCorrelations[a] = 1 - ( partialmatrix.frobenius_norm() ) / matpfrobnorm;
      VectorType evec = this->m_VariatesP.get_column( a );
      this->m_VariatesP.set_column( a, zero );
      //      if ( overit == 0 ) ( void ) this->PowerIteration(  partialmatrix,  evec, 5, true );
      this->m_CanonicalCorrelations[a] = this->IHTPowerIteration(  partialmatrix,  evec, 5, a );    // 0 => a
      this->m_VariatesP.set_column( a, evec );
      matrixB.set_column( a, bvec );
      a++;
      // update B matrix by linear regression
      reconerr = this->SparseReconB( matrixB, icept  );
      } // while

// filter / join eigenvectors ...
/** do here
    MatrixType correspondencematrix(  this->m_VariatesP.cols() ,  this->m_VariatesP.cols() );
    correspondencematrix.fill( 0 );
    for ( unsigned int ioiner = 0; ioiner < this->m_VariatesP.cols(); ioiner++ )
      for ( unsigned int joiner = (ioiner+1); joiner < this->m_VariatesP.cols(); joiner++ )
        {
        correspondencematrix( ioiner, joiner ) = this->PearsonCorr(
          this->m_MatrixP * this->m_VariatesP.get_column( ioiner ),
    this->m_MatrixP * this->m_VariatesP.get_column( joiner ) );
  if ( sparsenessparams( ioiner ) > 0.1 || sparsenessparams( joiner ) > 0.1 )
    correspondencematrix( ioiner, joiner ) = 0;
        }
    ::ants::antscout << " max corr " <<  correspondencematrix.max_value() << std::endl;
    if ( ( ( overit % 4 ) == 0 ) && (  correspondencematrix.max_value() > 0.1 )  )
    {
    unsigned int maxpair = correspondencematrix.arg_max();
    unsigned int maxrow = ( unsigned int )  maxpair / correspondencematrix.cols( );
    unsigned int maxcol = maxpair - maxrow * correspondencematrix.cols();
    if ( maxcol < maxrow ) { unsigned int temp = maxrow; maxrow = maxcol; maxcol = temp; }
    VectorType sumvar = this->m_VariatesP.get_column( maxrow ) + this->m_VariatesP.get_column( maxcol );
    this->m_VariatesP.set_column( maxrow , sumvar );
    sparsenessparams( maxrow ) =  sparsenessparams( maxrow ) +  sparsenessparams( maxcol );
    VectorType initvec = this->InitializeV( this->m_MatrixP, maxcol + 1 );
    for( unsigned int j = 0; j < maxcol; j++ ) initvec = this->Orthogonalize( initvec, this->m_VariatesP.get_column( j ) );
    initvec = this->SpatiallySmoothVector( initvec, this->m_MaskImageP );
    initvec = initvec / initvec.two_norm( );
    this->m_VariatesP.set_column( maxcol , initvec );
    ::ants::antscout << " max corr " <<  correspondencematrix( maxrow , maxcol ) << " sp " << sparsenessparams( maxrow ) << " fusing " << maxrow << " & " << maxcol << std::endl;
    correspondencematrix( maxrow , maxcol ) = 0;
    correspondencematrix( maxcol , maxrow ) = 0;
    sparsenessparams = sparsenessparams / sparsenessparams.sum();
    }
*/
    ::ants::antscout << overit << ": %var " << reconerr << std::endl;
    }
  this->m_VariatesQ = matrixB;
  this->SortResults( n_vecs );
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    VectorType v = this->m_VariatesP.get_column( i );
    if( v.min_value() < 0 )
      {
      this->m_VariatesP.set_column( i, v * ( -1 ) );
      }
    }
  return 1.0 / reconerr;

  /*
  // get 1st eigenvector ... how should this be done?  how about svd?
  // if ( overit == 0 )
  // if ( ( a % 2  ) == 0 ) this->PosNegVector( evec, true );
  // else this->PosNegVector( evec, false );
  this->m_CanonicalCorrelations[ a ] = 0;
  for( unsigned int ii = 0; ii < 20; ii++ )
{
VectorType initvec = this->InitializeV( partialmatrix, ii + 1 );
initvec = this->SpatiallySmoothVector( initvec, this->m_MaskImageP );
    RealType rayquo = this->IHTPowerIteration(  partialmatrix,  initvec, 3  , a );//this->PowerIteration(  partialmatrix,  initvec, 3, true );
if ( rayquo > this->m_CanonicalCorrelations[ a ] )
{
this->m_CanonicalCorrelations[ a ] = rayquo;
evec = initvec;
}
}*/

  /*
  for(  unsigned int a = 0; a < n_vecs; a++ )
    {
    VectorType nvec = matrixB.get_column( a );
    VectorType pvec = this->m_VariatesP.get_column( a );
    MatrixType recon = this->m_MatrixP - outer_product( nvec , pvec );
    RealType temp = recon.frobenius_norm();
    this->m_CanonicalCorrelations[ a ] = 1 / ( temp + 1 );
    }
  for(  unsigned int a = 0; a < n_vecs; a++ )
    {
    VectorType nvec = matrixB.get_column( a );
    this->m_CanonicalCorrelations[ a ] = nvec.one_norm();
    }
  */
  /** a regression-based method
  for(  unsigned int a = 0; a < this->m_MatrixP.cols(); a++ )
    {
    VectorType x_i = this->m_MatrixP.get_column( a );
    VectorType lmsolv = this->m_VariatesP.get_row( a ); // good initialization should increase convergence speed
    (void) this->ConjGrad(  matrixB, lmsolv, x_i, 0, 10000 );    // A x = b
    VectorType x_recon = ( matrixB * lmsolv + this->m_Intercept );
    onenorm += x_i.one_norm() / this->m_MatrixP.rows();
    reconerr += ( x_i - x_recon ).one_norm() / this->m_MatrixP.rows();
    this->m_VariatesP.set_row( a , lmsolv );
    }
  */

  /* a gradient method
  E & = \langle X - U V^T , X - U V^T \rangle \\
  \partial E/\partial V & = \langle -U , X - U V^T \rangle \\
  & = -U^T X + U^T U V^T \rangle
  if ( overit == 0 ) this->m_VariatesP.fill( 0 );
  MatrixType vgrad = matrixB.transpose() * this->m_MatrixP -
    ( matrixB.transpose() * matrixB ) * this->m_VariatesP.transpose();
  //  this->m_VariatesP = this->m_VariatesP + vgrad.transpose() * 0.01;
  this->m_VariatesP = this->m_VariatesP + vgrad.transpose() ;
  for(  unsigned int a = 0; a < this->m_MatrixP.rows(); a++ )
    {
    VectorType evec = this->m_VariatesP.get_column( a );
    this->SparsifyP( evec );
    this->m_VariatesP.set_column( a, evec );
    }
  */
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::IHTPowerIterationHome( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
                         typename antsSCCANObject<TInputImage, TRealType>::VectorType& evec,
                         // typename antsSCCANObject<TInputImage, TRealType>::VectorType& y,
                         unsigned int maxits, unsigned int maxorth )
{
  /** This computes a hard-thresholded gradient descent on the eigenvector criterion.
      max x  over  x^t A^t A x s.t.  x^t x = 1
      success of the optimization is measured by rayleigh quotient. derivative is:
      d.dx ( x^t A^t A x  ) =   A^t A x  ,   x \leftarrow  x / \| x \|
      we use a conjugate gradient version of this optimization.
  */
  if( evec.two_norm() ==  0 )
    {
    evec = this->InitializeV( this->m_MatrixP, true );
    }
  VectorType proj = ( A * evec  );
  VectorType lastgrad = evec;
  RealType   rayquo = 0, rayquold = -1;
  RealType   denom = inner_product( evec, evec );
  if( denom > 0 )
    {
    rayquo = inner_product( proj, proj  ) / denom;
    }
  MatrixType   At = A.transpose();
  unsigned int powerits = 0;
  bool         conjgrad = true;
  VectorType   bestevec = evec;
  while( ( ( rayquo > rayquold ) && ( powerits < maxits ) )  )
    {
    VectorType nvec = At * proj;
    for( unsigned int orth = 0; orth < maxorth; orth++ )
      {
      nvec = this->Orthogonalize( nvec, this->m_VariatesP.get_column( orth ) );
      }
    RealType gamma = 0.1;
    nvec = this->SpatiallySmoothVector( nvec, this->m_MaskImageP );
    if( ( lastgrad.two_norm() > 0  ) && ( conjgrad ) )
      {
      gamma = inner_product( nvec, nvec ) / inner_product( lastgrad, lastgrad );
      }
    lastgrad = nvec;
    evec = evec * gamma + nvec;
    evec = this->SpatiallySmoothVector( evec, this->m_MaskImageP );
    this->CurvatureSparseness( evec,  ( 1 - this->m_FractionNonZeroP ) * 100, 5 , this->m_MaskImageP );

    this->SparsifyP( evec );
    // VectorType gradvec = this->ComputeVectorGradMag( evec, this->m_MaskImageP );
    if( evec.two_norm() > 0 )
      {
      evec = evec / evec.two_norm();
      }
    proj = ( A * evec  );
    rayquold = rayquo;
    denom = inner_product( evec, evec );
    if( denom > 0 )
      {
      rayquo = inner_product( proj, proj  ) / denom; // - gradvec.two_norm() / gradvec.size() * 1.e2 ;
      }
    powerits++;
    if( rayquo > rayquold )
      {
      bestevec = evec;
      }
    }

  evec = bestevec;
  ::ants::antscout << "rayleigh-quotient: " << rayquo << " in " << powerits << " num " << maxorth << std::endl;
  return rayquo;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseReconHome(unsigned int n_vecs)
{
  //  this->m_UseL1 = false;
  RealType reconerr = 0;
  RealType onenorm = 0;

  this->m_CanonicalCorrelations.set_size( n_vecs );
  this->m_CanonicalCorrelations.fill( 0 );
  ::ants::antscout << " sparse recon " << this->m_MinClusterSizeP << " useL1 " << this->m_UseL1  << std::endl;
  MatrixType matrixB( this->m_OriginalMatrixP.rows(), n_vecs );
  matrixB.fill( 0 );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );
  MatrixType        cov = this->m_MatrixP * this->m_MatrixP.transpose();
  vnl_svd<RealType> eig( cov, 1.e-6 );
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), n_vecs );
  this->m_VariatesP.fill( 0 );
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    if( i < this->m_MatrixP.rows() )
      {
      VectorType u = eig.U().get_column( i );
      VectorType up = u * this->m_MatrixP;
      this->SparsifyP( up );
      this->m_VariatesP.set_column( i, up );
      matrixB.set_column( i,  u );
      }
    else
      {
      this->m_VariatesP.set_column( i, this->InitializeV( this->m_MatrixP, false ) );
      }
    }
  VectorType icept( this->m_MatrixP.rows(), 0 );
  RealType   matpfrobnorm = this->m_MatrixP.frobenius_norm();
  for( unsigned int overit = 0; overit < this->m_MaximumNumberOfIterations; overit++ )
    {
    //  cov = this->m_VariatesP.transpose() * this->m_VariatesP;
    //  vnl_svd<double> qr( cov );
    //  this->m_VariatesP = this->m_VariatesP * qr.U();
    // update V matrix
    /** a power iteration  method --- depends on the following

       given any nonzero $z \in \mathbb{R}^n$, the Rayleigh quotient
       $x^T X x / x^T x $ minimizes the function $\| \lambda x - X x \|^2 $
       wrt $\lambda$.

       so, if we find the vector x ( by sparse power iteration ) then we have a vector
       that is a close approximation to the first eigenvector of X. If X is a residual
       matrix then x is a good approximation of the $n^th$ eigenvector.

    **/
    VectorType zero( this->m_MatrixP.cols(), 0 );
    VectorType zerob( this->m_MatrixP.rows(), 0 );
    for(  unsigned int a = 0; a < n_vecs; a++ )
      {
      VectorType bvec = matrixB.get_column( a );
      matrixB.set_column( a, zerob );
      MatrixType tempMatrix = this->m_VariatesP;
      tempMatrix.set_column( a, zero );
      MatrixType partialmatrix = matrixB * tempMatrix.transpose();
      for(  unsigned int interc = 0; interc < this->m_MatrixP.rows(); interc++ )
        {
        partialmatrix.set_row( interc, partialmatrix.get_row( interc ) + icept( interc ) );
        }
      this->m_CanonicalCorrelations[a] = ( partialmatrix.frobenius_norm() ) / matpfrobnorm;
      partialmatrix = this->m_MatrixP - partialmatrix;
      VectorType evec = this->m_VariatesP.get_column( a );
      this->m_VariatesP.set_column( a, zero );
      if( evec.two_norm()  > 0 )
        {
        evec = evec / evec.two_norm();
        }

      // get 1st eigenvector ... how should this be done?  how about svd?
      if( ( a >= this->m_MatrixP.rows() )  &&  ( overit == 0 ) )
        {
        ( void ) this->PowerIteration(  partialmatrix,  evec, 10, false );
        }
      this->m_CanonicalCorrelations[a] = this->IHTPowerIterationHome(  partialmatrix,  evec, 20, 0 );     // 0 => a
      this->m_VariatesP.set_column( a, evec );
      matrixB.set_column( a, bvec );
      }

    // update B matrix by linear regression
    reconerr = onenorm = 0;
    icept.fill( 0 );
    for(  unsigned int a = 0; a < this->m_MatrixP.rows(); a++ )
      {
      VectorType x_i = this->m_MatrixP.get_row( a );
      VectorType lmsolv = matrixB.get_row( a );                           // good initialization should increase
                                                                          // convergence speed
      (void) this->ConjGrad(  this->m_VariatesP, lmsolv, x_i, 0, 10000 ); // A x = b
      VectorType x_recon = ( this->m_VariatesP * lmsolv + this->m_Intercept );
      icept( a ) = this->m_Intercept;
      onenorm += x_i.one_norm() / this->m_MatrixP.cols();
      reconerr += ( x_i - x_recon ).one_norm() / this->m_MatrixP.cols();
      matrixB.set_row( a, lmsolv );
      }
    RealType rr = ( onenorm - reconerr ) / onenorm;
    ::ants::antscout << overit << ": %var " << rr << " raw-reconerr " << reconerr << std::endl;
    }
  this->m_VariatesQ = matrixB;
  this->SortResults( n_vecs );
  //  this->m_UseL1 = false;
  return 1.0 / reconerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseArnoldiSVDGreedy(unsigned int n_vecs)
{
  ::ants::antscout << " arnoldi sparse svd : greedy " << this->m_MinClusterSizeP << " sparseness "
                   <<  this->m_FractionNonZeroP << std::endl;
  std::vector<RealType> vexlist;

  vexlist.push_back( 0 );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    ::ants::antscout << " Subtracting nuisance matrix from P matrix " << std::endl;
    this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
    }
  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  MatrixType bmatrix = this->GetCovMatEigenvectors( this->m_MatrixP );
  MatrixType bmatrix_big;
  bmatrix_big.set_size( this->m_MatrixP.cols(), n_vecs );
  //  double trace = vnl_trace<double>(   this->m_MatrixP * this->m_MatrixP.transpose()  );
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    this->m_VariatesP.set_column( kk, this->InitializeV( this->m_MatrixP )  );
    if( kk < bmatrix.columns() && true )
      {
      VectorType initv = bmatrix.get_column( kk ) * this->m_MatrixP;
      this->SparsifyP( initv );
      this->m_VariatesP.set_column( kk, initv );
      }
    }
  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
// Arnoldi Iteration SVD/SPCA
  unsigned int loop = 0;
  bool         debug = false;
  double       convcrit = 1;
  RealType     fnp = 1;
  bool         negate = false;
  MatrixType   lastV = this->m_VariatesP;  lastV.fill( 0 );
  while( loop<maxloop && (convcrit)> 1.e-8 )
    {
    /** Compute the gradient estimate according to standard Arnoldi */
    fnp = this->m_FractionNonZeroP;
    // MatrixType cov = this->m_VariatesP.transpose() * this->m_VariatesP;
    // vnl_svd<double> qr( cov );
    // this->m_VariatesP = this->m_VariatesP * qr.U();
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType pveck = this->m_VariatesP.get_column(k);
      MatrixType pmod = this->NormalizeMatrix( this->m_OriginalMatrixP );
      if( k > 1  && loop > 1 && k < this->m_MatrixP.rows() - 1 && false  )
        {
        MatrixType m( this->m_MatrixP.rows(), k, 0 );
        for( unsigned int mm = 0; mm < k; mm++ )
          {
          m.set_column( mm, this->m_MatrixP * this->m_VariatesP.get_column( mm ) );
          }
        MatrixType projmat = this->ProjectionMatrix( m, 1.e-8 );
        pmod = pmod - projmat * pmod;
        }
      for( unsigned int powerit = 0; powerit < 5; powerit++ )
        {
        pveck = ( pmod * pveck ); // classic
        pveck  = pmod.transpose() * ( pveck  );
        RealType hkkm1 = pveck.two_norm();
        if( hkkm1 > 0 )
          {
          pveck = pveck / hkkm1;
          }
        for( unsigned int j = 0; j < k; j++ )
          {
          VectorType qj = this->m_VariatesP.get_column(j);
          pveck = this->Orthogonalize( pveck, qj );
          }
        } // powerit
          /** penalize by gradient
          RealType spgoal = ( 1 - fnp ) * 100 ;
          this->CurvatureSparseness( pveck , spgoal );*/

      /** Project to the feasible sub-space */
      if( negate )
        {
        pveck = pveck * ( -1 );
        }
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( pveck, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( pveck, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( pveck, this->m_MaskImageP, this->m_MinClusterSizeP );
        if( pveck.two_norm() > 0 )
          {
          pveck = pveck / pveck.two_norm();
          }
        }
      if( negate )
        {
        pveck = pveck * ( -1 );
        }
      /********************************
      unsigned int bcolind = 0;
      RealType maxcorr = 0;
      for ( unsigned int cc = 0 ; cc < bmatrix_big.cols(); cc ++ )
        {
        RealType corr = fabs( this->PearsonCorr( this->m_MatrixP * pveck , bmatrix.get_column( cc ) ) );
        if (  corr > maxcorr ) bcolind = cc;
        }
      VectorType b = bmatrix_big.get_column( bcolind ) ;
      RealType cgerr = this->SparseConjGrad( pveck , b , 1.e-3 );
      ********************************/
      /** Update the solution */
      this->m_VariatesP.set_column(k, pveck);
      } // kloop
        // compute the reconstruction error for each voxel
        /* Now get the LSQ regression solution
    X - n \times p matrix of  subjects by voxels thickness measures

    B - k \times p  matrix of roi measures --- some row has p voxels and that row is binary i.e. non-zero in the voxels where that label is non-zero ( out of e.g. the whole cortex)

    \beta - 1 \times k vector of beta values computed from the regression solution

    X_i  --- 1 \times n some voxel for each subject

    then solve , for each voxel ,

    \| X_i - \beta B    X \|
      1xn      1xk kxp pxn

    \| X   - \Beta B X \|
      pxn    pxk  kxp pxn


      voxels  -

    requires you to have  n > k --- which you should have .... and easily done in R b.c you precomputed    B X =>  a   k \times n matrix

    this means that you can reconstruct all voxels for a subject given the measures of his ROI values --- so it's basically a regression across all voxels given the ROI ( or PCA ) values from that subject.   the reconstruction error is very descriptive about the information gained by the dimensionality reduction used for that data.
        RealType totalerr = 0;
        MatrixType A = ( this->m_MatrixP * this->m_VariatesP );
        MatrixType reconmat( this->m_MatrixP.rows(),  this->m_MatrixP.cols(), 0 );
        for ( unsigned int vox = 0; vox < this->m_MatrixP.cols(); vox++ )
          {
          VectorType lmsol( n_vecs, 0);
          VectorType voxels = this->m_MatrixP.get_column( vox );
          RealType  lmerror = this->ConjGrad(  A ,  lmsol , voxels , 0 , 100 );
          VectorType proj =   A * lmsol;
          if ( vnl_math_isnan( lmerror ) ) { lmerror = 0 ; proj.fill(0); }
          reconmat.set_column( vox , proj );
          totalerr += lmerror;
          }
        totalerr /= this->m_MatrixP.cols() ;
        ::ants::antscout << " totalerr " << totalerr << std::endl;
       {
        VectorType vvv = reconmat.get_row( 10 );
        typename TInputImage::Pointer image=this->ConvertVariateToSpatialImage( vvv , this->m_MaskImageP , false );
        typedef itk::ImageFileWriter<TInputImage> WriterType;
        typename WriterType::Pointer writer = WriterType::New();
        writer->SetFileName( "temp.nii.gz" );
        writer->SetInput( image );
        writer->Update();
        }
        {
        VectorType vvv = this->m_MatrixP.get_row( 10 );
        typename TInputImage::Pointer image=this->ConvertVariateToSpatialImage( vvv , this->m_MaskImageP , false );
        typedef itk::ImageFileWriter<TInputImage> WriterType;
        typename WriterType::Pointer writer = WriterType::New();
        writer->SetFileName( "temp2.nii.gz" );
        writer->SetInput( image );
        writer->Update();
        }
        */
    this->m_VariatesQ = this->m_VariatesP;
    /** Estimate eigenvalues , then sort */

    RealType bestvex = // this->ComputeSPCAEigenvalues( n_vecs, trace, false );
      this->ReconstructionError( this->m_MatrixP, this->m_VariatesP );
    /* begin the crude line search */
    if( bestvex < vexlist[loop]  )
      {
      RealType f = -0.5, enew = 0, eold = -1, delt = 0.05;
      bestvex = 0;
      MatrixType bestV = this->m_VariatesP;
      while(  ( enew - eold ) >  0  && f <= 0.5 && loop > 0 && true )
        {
        this->m_VariatesP = ( this->m_VariatesQ * f  + lastV * ( 1 - f ) );
        for( unsigned int m = 0; m < this->m_VariatesP.cols(); m++ )
          {
          VectorType mm = this->m_VariatesP.get_column(m);
          this->SparsifyP( mm );
          if( mm.two_norm() > 0  )
            {
            mm = mm / mm.two_norm();
            }
          this->m_VariatesP.set_column( m, mm  );
          }
        eold = enew;
        enew = this->ReconstructionError( this->m_MatrixP, this->m_VariatesP );
        if( enew > bestvex )
          {
          bestvex = enew;  bestV = this->m_VariatesP;
          }
        // ::ants::antscout <<" vex " << enew << " f " << f << std::endl;
        f = f + delt;
        }

      this->m_VariatesP = bestV;
      }
    if( bestvex < vexlist[loop] )
      {
      this->m_VariatesP = lastV; bestvex = vexlist[loop];
      }

    lastV = this->m_VariatesP;
    RealType vex = bestvex;
    vexlist.push_back( vex );
    this->SortResults(n_vecs);
    convcrit = ( this->ComputeEnergySlope(vexlist, 10) );
    if( bestvex == vexlist[loop] )
      {
      convcrit = 0;
      }
    ::ants::antscout << "Iteration: " << loop << " Eval_0: " << this->m_CanonicalCorrelations[0] << " Eval_N: "
                     << this->m_CanonicalCorrelations[n_vecs
                                     - 1] << " Sp: " << fnp  << " conv-crit: " << convcrit <<  " %var " << bestvex
                     << std::endl;
    loop++;
    if( debug )
      {
      ::ants::antscout << "wloopdone" << std::endl;
      }
    } // opt-loop
  for( unsigned int i = 0; i < vexlist.size(); i++ )
    {
    ::ants::antscout << vexlist[i] << ",";
    }
  ::ants::antscout << std::endl;
  return fabs(this->m_CanonicalCorrelations[0]);
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseArnoldiSVD_x(unsigned int n_vecs)
{
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  ::ants::antscout << " arnoldi sparse svd " << std::endl;
  std::vector<RealType> vexlist;
  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    if( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )
      {
      ::ants::antscout << " Subtracting nuisance matrix from P matrix " << std::endl;
      this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
      }
    }
  this->m_ClusterSizes.set_size(n_vecs);
  this->m_ClusterSizes.fill(0);
  double trace = 0;
  for( unsigned int i = 0; i < this->m_MatrixP.cols(); i++ )
    {
    trace += inner_product(this->m_MatrixP.get_column(i), this->m_MatrixP.get_column(i) );
    }
  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  VariateType myGradients;
  this->m_SparseVariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_SparseVariatesP.fill(0);
  myGradients.set_size(this->m_MatrixP.cols(), n_vecs);
  myGradients.fill(0);
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    this->m_VariatesP.set_column(kk, this->InitializeV(this->m_MatrixP) );
    }
  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
// Arnoldi Iteration SVD/SPCA
  unsigned int loop = 0;
  bool         debug = false;
  double       convcrit = 1;
  RealType     fnp = 1;
  while( loop<maxloop && (convcrit)> 1.e-8 )
    {
    /** Compute the gradient estimate */
    fnp = this->m_FractionNonZeroP;
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      // the old solution
      VectorType pveck = this->m_VariatesP.get_column(k);
      pveck  = this->m_MatrixP.transpose() * ( this->m_MatrixP  * pveck );
      RealType hkkm1 = pveck.two_norm();
      if( hkkm1 > 0 )
        {
        pveck = pveck / hkkm1;
        }
      for( unsigned int j = 0; j < k; j++ )
        {
        VectorType qj = this->m_VariatesP.get_column(j);
        RealType   ip = inner_product(qj, qj);
        if( ip < this->m_Epsilon )
          {
          ip = 1;
          }
        RealType hjk = inner_product(qj, pveck) / ip;
        pveck = pveck - qj * hjk;
        }
      // pveck is now the standard arnoldi pca update to the solution
      VectorType ppp(pveck);
      if( fnp < 1  )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( ppp, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( ppp, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( ppp, this->m_MaskImageP, this->m_MinClusterSizeP );
        ppp = ppp / ppp.two_norm();
        }
      myGradients.set_column(k, ppp - pveck);
      myGradients.set_column(k, ppp);
      if( loop == 0 )
        {
        this->m_VariatesP.set_column(k, pveck);
        }
      if( loop == 0 )
        {
        myGradients.set_column(k, pveck);
        }
      //      this->m_VariatesP.set_column(k, this->m_SparseVariatesP.get_column(k) );
      // this->m_SparseVariatesP.set_column(k,pveck);
      } // kloop

    /** Update the full solution space */
    double recuravg = .9; // /sqrt(sqrt((double)(loop+1)));
    double recuravg2 = 1 - recuravg;
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType pveck  = myGradients.get_column(k);
      VectorType newsol = this->m_SparseVariatesP.get_column(k) * recuravg2 + pveck * recuravg;
      this->m_SparseVariatesP.set_column(k, newsol); //   /newsol.two_norm()
      }
    /** Update the projected solution in sub-space */
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType pveck = this->m_SparseVariatesP.get_column(k);
      pveck  = this->m_MatrixP.transpose() * ( this->m_MatrixP  * pveck );
      RealType hkkm1 = pveck.two_norm();
      if( hkkm1 > 0 )
        {
        pveck = pveck / hkkm1;
        }
      for( unsigned int j = 0; j < k; j++ )
        {
        VectorType qj = this->m_VariatesP.get_column(j);
        RealType   ip = inner_product(qj, qj);
        if( ip < this->m_Epsilon )
          {
          ip = 1;
          }
        RealType hjk = inner_product(qj, pveck) / ip;
        pveck = pveck - qj * hjk;
        }
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( pveck, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( pveck, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( pveck, this->m_MaskImageP, this->m_MinClusterSizeP );
        pveck = pveck / pveck.two_norm();
        }
      this->m_VariatesP.set_column(k, pveck);
      } // kloop for update

    this->m_VariatesQ = this->m_VariatesP;
    if( debug )
      {
      ::ants::antscout << " get evecs " << std::endl;
      }
    RealType vex = this->ComputeSPCAEigenvalues(n_vecs, trace, true);
    vexlist.push_back(   vex    );
    this->SortResults(n_vecs);
    convcrit = ( this->ComputeEnergySlope(vexlist, 5) );
    ::ants::antscout << "Iteration: " << loop << " Eigenval_0: " << this->m_CanonicalCorrelations[0] << " Eigenval_N: "
                     << this->m_CanonicalCorrelations[n_vecs
                                     - 1] << " Sparseness: " << fnp  << " convergence-criterion: " << convcrit
                     <<  " vex " << vex << std::endl;
    loop++;
    if( debug )
      {
      ::ants::antscout << "wloopdone" << std::endl;
      }
    } // opt-loop
  for( unsigned int i = 0; i < vexlist.size(); i++ )
    {
    ::ants::antscout << vexlist[i] << ",";
    }
  ::ants::antscout << std::endl;
  return fabs(this->m_CanonicalCorrelations[0]);
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::ConjGrad( typename antsSCCANObject<TInputImage,
                                     TRealType>::MatrixType& A,
            typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
            typename antsSCCANObject<TInputImage, TRealType>::VectorType  b_in, TRealType convcrit,
            unsigned int maxits)
{
  /** This will use the normal equations */
  /** Based on Golub CONJUGATE  G R A D I E N T   A N D  LANCZOS  HISTORY
   *  http://www.matematicas.unam.mx/gfgf/cg2010/HISTORY-conjugategradient.pdf
   */
  bool debug = false;
  // minimize the following error :    \| A^T*A * vec_i -    b \|  +  sparseness_penalty
  MatrixType At = A.transpose();
  VectorType b = b_in - b_in.mean();
  b = b * A;
  VectorType r_k = At * ( A * x_k );
  r_k = b - r_k;
  VectorType   p_k = r_k;
  double       approxerr = 1.e9;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = b.two_norm();
  RealType     minerr = starterr, deltaminerr = 1, lasterr = starterr * 2;
  while(  deltaminerr > 0 && approxerr > convcrit && ct < maxits )
    {
    RealType alpha_denom = inner_product( p_k, At * ( A * p_k ) );
    RealType iprk = inner_product( r_k, r_k );
    if( debug )
      {
      ::ants::antscout << " iprk " << iprk << std::endl;
      }
    if( alpha_denom < 1.e-12 )
      {
      alpha_denom = 1;
      }
    RealType alpha_k = iprk / alpha_denom;
    if( debug )
      {
      ::ants::antscout << " alpha_k " << alpha_k << std::endl;
      }
    VectorType x_k1  = x_k + alpha_k * p_k; // this adds the scaled residual to the current solution
    this->SparsifyOther( x_k1 ); // this can be used to approximate NMF
    VectorType r_k1 =  b - At * (A * x_k1 );
    approxerr = r_k1.two_norm();
    if( approxerr < minerr )
      {
      minerr = approxerr; bestsol = ( x_k1 );
      }
    deltaminerr = ( lasterr - approxerr );
    if( debug )
      {
      ::ants::antscout << " ConjGrad " << approxerr <<  " minerr " << minerr << std::endl;
      }
    lasterr = approxerr;
    // measures the change in the residual --- this is the Fletcher-Reeves form for nonlinear CG
    // see Table 1.1 \Beta^FR in A SURVEY OF NONLINEAR CONJUGATE GRADIENT METHODS
    // in this paper's notation d => p,  g => r , alpha, beta, x the same , so y = rk1 - rk
    // measures the change in the residual
    VectorType yk = r_k1 - r_k;
    //RealType  bknd =  inner_product( p_k, yk );
    //RealType  beta_k = inner_product( ( yk - p_k * 2 * yk.two_norm() / bknd ) , r_k1 / bknd ); // Hager and Zhang
    RealType   beta_k = inner_product( r_k1, r_k1 ) /  inner_product( r_k, r_k ); // classic cg
    VectorType p_k1  = r_k1 + beta_k * p_k;
    if( debug )
      {
      ::ants::antscout << " p_k1 " << p_k1.two_norm() << std::endl;
      }
    r_k = r_k1;
    p_k = p_k1;
    x_k = x_k1;
    ct++;
    }

  x_k = bestsol;
  this->m_Intercept = this->ComputeIntercept( A, x_k, b_in );
  VectorType soln = A * x_k + this->m_Intercept;
  return ( soln - b_in).one_norm() / b_in.size();
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::EvaluateEnergy( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
                  typename antsSCCANObject<TInputImage, TRealType>::VectorType&  x_k,
                  typename antsSCCANObject<TInputImage, TRealType>::VectorType&  p_k,
                  typename antsSCCANObject<TInputImage, TRealType>::VectorType&  b,
                  TRealType minalph, TRealType lambda )
{
  VectorType x_k1  = x_k + minalph * p_k;

  this->SparsifyP( x_k1 );
  VectorType r_k1 = ( b -  A.transpose() * ( A * x_k1 )  );
  RealType   e = r_k1.two_norm() + x_k1.two_norm() * lambda;
  return e;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::GoldenSection( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
                 typename antsSCCANObject<TInputImage, TRealType>::VectorType&  x_k,
                 typename antsSCCANObject<TInputImage, TRealType>::VectorType&  p_k,
                 typename antsSCCANObject<TInputImage, TRealType>::VectorType&  bsol,
                 TRealType a, TRealType b, TRealType c, TRealType tau, TRealType lambda )
{
  if( this->m_GoldenSectionCounter == 0 )
    {
    this->m_GSBestSol = a;
    }
  this->m_GoldenSectionCounter++;
  if( this->m_GoldenSectionCounter > 20 )
    {
    return this->m_GSBestSol;
    }
  double phi = (1 + sqrt(5.0) ) / 2;
  double resphi = 2 - phi;
  double x;
  if( c - b > b - a )
    {
    x = b + resphi * (c - b);
    }
  else
    {
    x = b - resphi * (b - a);
    }
  if( fabs(c - a) < tau * ( fabs(b) + fabs(x) ) )
    {
    return (c + a) / 2;
    }
  if( this->EvaluateEnergy( A, x_k, p_k, bsol, x, lambda ) < this->EvaluateEnergy( A, x_k, p_k, bsol, b, lambda  ) )
    {
    this->m_GSBestSol = x;
    if( c - b > b - a )
      {
      return GoldenSection( A, x_k, p_k, bsol, b, x, c, tau, lambda );
      }
    else
      {
      this->m_GSBestSol = b;
      return GoldenSection( A, x_k, p_k, bsol, a, x, b, tau, lambda );
      }
    }
  else
    {
    if( c - b > b - a )
      {
      return GoldenSection( A, x_k, p_k, bsol, a, b, x, tau, lambda );
      }
    else
      {
      return GoldenSection( A, x_k, p_k, bsol, x, b, c, tau, lambda );
      }
    }
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::LineSearch( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
              typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
              typename antsSCCANObject<TInputImage, TRealType>::VectorType& p_k,
              typename antsSCCANObject<TInputImage, TRealType>::VectorType& b,
              TRealType minalph, TRealType maxalph, TRealType lambda )
{
  bool dogs = true;

  if( dogs )
    {
    RealType big = maxalph;
    this->m_GoldenSectionCounter = 0;
    RealType tau = 0.00001; // sets accuracy , lower => more accurate
    return this->GoldenSection( A, x_k, p_k, b, 0, big / 2, big, tau, lambda);
    }

  // otherwise just search across a line ...
  RealType minerr = 1.e99;
  RealType bestalph = 0;
  RealType step;
  step =  ( maxalph - minalph ) / 10;
  for( RealType eb = minalph; eb <= maxalph; eb = eb + step )
    {
    RealType e = this->EvaluateEnergy( A, x_k, p_k, b, eb, lambda );
    if( e < minerr )
      {
      minerr = e; bestalph = eb;
      }
    }
  return bestalph;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseNLConjGrad( typename antsSCCANObject<TInputImage,
                                             TRealType>::MatrixType& A,
                    typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
                    typename antsSCCANObject<TInputImage,
                                             TRealType>::VectorType  b, TRealType convcrit, unsigned int maxits,
                    bool makeprojsparse,  unsigned int loorth, unsigned int hiorth )
{
  bool negate = false;

  if( b.max_value() <= 0 )
    {
    negate = true;
    }
  if( negate )
    {
    b = b * ( -1 );
    }
  bool     debug = false;
  RealType intercept = 0;

  VectorType   r_k = ( b -  A.transpose() * ( A * x_k ) );
  VectorType   p_k = r_k;
  double       approxerr = 1.e22;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = r_k.two_norm();
  RealType     minerr = starterr, deltaminerr = 1;
  while(  deltaminerr > 1.e-4 && minerr > convcrit && ct < maxits )
    {
    RealType alpha_denom = inner_product( p_k,  A.transpose() * ( A * p_k ) );
    RealType iprk = inner_product( r_k, r_k );
    RealType alpha_k = iprk / alpha_denom;
    if( debug )
      {
      ::ants::antscout << " alpha_k " << alpha_k << " iprk " << iprk <<  " alpha_denom " << alpha_denom << std::endl;
      }
    // HACK NOT USED RealType stepsize = alpha_k / 100;
    // HACK NOT USED RealType minalph = stepsize;
    // HACK NOT USED RealType maxalph = alpha_k * 2;
    RealType best_alph = alpha_k;
    //    RealType   best_alph = this->LineSearch( A, x_k, p_k, b, minalph, maxalph, keeppos );
    VectorType x_k1  = x_k + best_alph * p_k;
    // this adds the scaled residual to the current solution
    if( debug )
      {
      ::ants::antscout << " xk12n " << x_k1.two_norm() << " alpha_k " << alpha_k << " pk2n " << p_k.two_norm()
                       << " xk1-min " << x_k1.min_value() << std::endl;
      }
    if( hiorth > loorth   )
      {
      for(  unsigned int wv = loorth; wv < hiorth; wv++ )
        {
        x_k1 = this->Orthogonalize( x_k1, this->m_VariatesP.get_column( wv ) );
        }
      }

    this->SparsifyP( x_k1 );
    if( debug )
      {
      ::ants::antscout << " xk12n " << x_k1.two_norm() << " alpha_k " << alpha_k << " pk2n " << p_k.two_norm()
                       << std::endl;
      }
    VectorType proj = A.transpose() * (A * x_k1 );
    VectorType r_k1 = ( b -  proj );
    if( makeprojsparse  )
      {
      this->SparsifyP( r_k1  );
      }
    approxerr = r_k1.two_norm();
    RealType newminerr = minerr;
    if( approxerr < newminerr || ct == 0 )
      {
      newminerr = approxerr; bestsol = ( x_k1 );  this->m_Intercept = intercept;
      }
    deltaminerr = ( minerr - newminerr  );
    if( newminerr < minerr )
      {
      minerr = newminerr;
      }
    if( debug )
      {
      ::ants::antscout << " r_k12n " << r_k1.two_norm() << " r_k2n " << r_k.two_norm() << std::endl;
      }
    if( debug )
      {
      ::ants::antscout << " x_k2n " << x_k.two_norm() << " x_k12n " << x_k1.two_norm() << std::endl;
      }
    if( debug )
      {
      ::ants::antscout << " r_k1 " << minerr <<  " derr " << deltaminerr << " ba " << best_alph / alpha_k << std::endl;
      }
    // measures the change in the residual --- this is the Fletcher-Reeves form for nonlinear CG
    // see Table 1.1 \Beta^FR in A SURVEY OF NONLINEAR CONJUGATE GRADIENT METHODS
    // in this paper's notation d => p,  g => r , alpha, beta, x the same , so y = rk1 - rk
    //    RealType   beta_k = inner_product( r_k1 , r_k1 ) /  inner_product( r_k , r_k ); // classic cg
    VectorType yk = r_k1 - r_k;
    RealType   bknd =  inner_product( p_k, yk );
    RealType   beta_k = inner_product( ( yk - p_k * 2 * yk.two_norm() / bknd ), r_k1 / bknd ); // Hager and Zhang
    // RealType beta_k = inner_product( r_k1 , r_k1 ) /  inner_product( r_k , r_k ); // classic cg
    VectorType p_k1  = r_k1 + beta_k * p_k;
    // if ( makeprojsparse ) this->SparsifyP( p_k1, keeppos );
    r_k = r_k1;
    p_k = p_k1;
    x_k = x_k1;
    ct++;
    }

  x_k = bestsol;
  if( negate )
    {
    x_k = x_k * ( -1 );
    b = b * ( -1 );
    }
  return minerr / A.rows();
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseNLPreConjGrad( typename antsSCCANObject<TInputImage,
                                                TRealType>::MatrixType& A,
                       typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
                       typename antsSCCANObject<TInputImage,
                                                TRealType>::VectorType  b, TRealType convcrit, unsigned int maxits )
{
  if( this->m_PreC.size() <= 0 )
    {
    vnl_diag_matrix<TRealType> prec( A.cols(), 0 );
    this->m_PreC = prec;
    for( unsigned int i = 0; i < A.cols(); i++ )
      {
      this->m_PreC( i, i ) = inner_product( A.get_column( i ),  A.get_column( i ) );
      if( this->m_PreC( i, i ) >  1.e-8 )
        {
        this->m_PreC( i, i ) = 1 / this->m_PreC( i, i );
        }
      else
        {
        this->m_PreC( i, i ) = 0;
        }
      }
    }

  RealType     intercept = 0;
  VectorType   r_k = ( b -  A.transpose() * ( A * x_k ) );
  VectorType   z_k = this->m_PreC * r_k;
  VectorType   p_k = z_k;
  double       approxerr = 1.e22;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = 1.e99;
  RealType     minerr = starterr, deltaminerr = 1;
  while( ( ct < 5 ) ||
         ( ( deltaminerr > 1.e-10 ) && ( minerr > convcrit ) && ( ct < maxits ) )
         )
    {
    //    RealType spgoal = 100.0 * ( 1 - vnl_math_abs( this->m_FractionNonZeroP ) );
    RealType alpha_denom = inner_product( p_k,  A.transpose() * ( A * p_k ) );
    RealType iprk = inner_product( r_k, z_k );
    RealType alpha_k = iprk / alpha_denom;
    RealType best_alph = alpha_k;
    //    RealType stepsize = alpha_k / 10;
    //    RealType minalph = stepsize;
    //    RealType maxalph = alpha_k * 4;
    // best_alph = this->LineSearch( A, x_k, p_k, b, minalph, maxalph, false );
    //    this->CurvatureSparseness( p_k , spgoal * 0.9 , A , b , 50 );
    VectorType x_k1  = x_k + best_alph * p_k;
    this->SparsifyP( x_k1 );
    // VectorType x_k2( x_k1 );
    // this->CurvatureSparseness( x_k1 , spgoal , 500 );
    //    x_k1 = x_k2;
    VectorType proj = A.transpose() * (A * x_k1 );
    VectorType r_k1 = ( b -  proj ) - x_k1 * 100;
    //    this->CurvatureSparseness( r_k1 , spgoal , A , b , 5 );
    approxerr = r_k1.two_norm();
    VectorType z_k1 = this->m_PreC * r_k1;
    RealType   newminerr = minerr;
    if( approxerr < newminerr || ct == 0 )
      {
      newminerr = approxerr; bestsol = ( x_k1 );  this->m_Intercept = intercept;
      }
    deltaminerr = ( minerr - newminerr  );
    if( newminerr < minerr )
      {
      minerr = newminerr;
      }
    VectorType yk = r_k1 - r_k;
    RealType   temp = inner_product( z_k, r_k );
    if( vnl_math_abs( temp ) < 1.e-9 )
      {
      temp = 1;
      }
    RealType   beta_k = inner_product( z_k1, r_k1 ) / temp;
    VectorType p_k1  = z_k1 + beta_k * p_k;
    r_k = r_k1;
    p_k = p_k1;
    x_k = x_k1;
    z_k = z_k1; // preconditioning
    ct++;
    ::ants::antscout << " approxerr " << approxerr << " ct " << ct << " nz " << this->CountNonZero( x_k )
                     << " deltaminerr " << deltaminerr << std::endl;
    }

  x_k = bestsol;
  RealType finalerr = minerr / A.rows();
  //  ::ants::antscout << " NZ1 " << this->CountNonZero( x_k ) << " err " << finalerr << std::endl;
  return finalerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::PowerIteration( typename antsSCCANObject<TInputImage,
                                           TRealType>::MatrixType& A,
                  typename antsSCCANObject<TInputImage, TRealType>::VectorType& evec, unsigned int maxits,
                  bool makesparse )
{
  unsigned int n_vecs = this->m_VariatesP.cols();

  for( unsigned int powerits = 0; powerits < maxits; powerits++ )
    {
    if( evec.two_norm() > 0 )
      {
      evec = evec / evec.two_norm();
      }
    evec = ( A.transpose() ) * ( A * evec ); // power iteration
    for( unsigned int orth = 0; orth < n_vecs; orth++ )
      {
      evec = this->Orthogonalize( evec, this->m_VariatesP.get_column( orth ) );
      }
    }
  //    (void) this->CurvatureSparseness( evec , 95 , 10 );
  if( makesparse )
    {
    this->SparsifyP( evec );
    }
  if( evec.two_norm() > 0 )
    {
    evec = evec / evec.two_norm();
    }
  return inner_product( A * evec, A * evec );
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::IHTPowerIteration( typename antsSCCANObject<TInputImage, TRealType>::MatrixType& A,
                     typename antsSCCANObject<TInputImage, TRealType>::VectorType& evecin,
                     // typename antsSCCANObject<TInputImage, TRealType>::VectorType& y,
                     unsigned int maxits, unsigned int maxorth )
{
  /** This computes a hard-thresholded gradient descent on the eigenvector criterion.
      max x  over  x^t A^t A x s.t.  x^t x = 1
      success of the optimization is measured by rayleigh quotient. derivative is:
      d.dx ( x^t A^t A x  ) =   A^t A x  ,   x \leftarrow  x / \| x \|
      we use a conjugate gradient version of this optimization.
  */
  VectorType evec = evecin;

  if( evecin.two_norm() ==  0 )
    {
    evec = this->InitializeV( this->m_MatrixP, false );
    }
  VectorType proj = ( A * evecin  );
  VectorType lastgrad = evecin;
  RealType   rayquo = 0;
  RealType   denom = inner_product( evecin, evecin );
  if( denom > 0 )
    {
    rayquo = inner_product( proj, proj  ) / denom;
    }
  RealType     bestrayquo = rayquo;
  MatrixType   At = A.transpose();
  unsigned int powerits = 0;
  bool         conjgrad = true;
  VectorType   bestevec = evecin;
  RealType     relfac = 1.0;
  VectorType di;
  while(  ( ( rayquo > bestrayquo ) && ( powerits < maxits ) )  || powerits < 2 )
  //  while ( ( relfac > 1.e-4  )  && ( powerits < maxits ) )
    {
    VectorType nvec = At * proj;
    RealType gamma = 0.1;
    //    nvec = this->SpatiallySmoothVector( nvec, this->m_MaskImageP );
    if ( powerits == 0 ) di = nvec;
    if( ( lastgrad.two_norm() > 0  ) && ( conjgrad ) )
      {
      gamma = inner_product( nvec, nvec ) / inner_product( lastgrad, lastgrad );
      }
    lastgrad = nvec;
    VectorType diplus1 =  nvec + di * gamma;
    evec = evec + diplus1 * relfac ;
    for( unsigned int orth = 0; orth < maxorth; orth++ )
      {
      VectorType zv = this->m_VariatesP.get_column( orth );
      evec = this->Orthogonalize( evec, zv );
      if ( this->m_Covering ) this->ZeroProduct( evec,  zv );
      /** alternative --- orthogonalize in n-space 
      VectorType v = A * this->m_VariatesP.get_column( orth );
      RealType ip1 = inner_product( A * evec,  v );
      RealType ip2 = inner_product( v, v );
      //      evec = evec - this->m_VariatesP.get_column( orth ) * ip1 / ip2;*/
      }
    this->SparsifyP( evec  );
    if( evec.two_norm() > 0 )
      {
      evec = evec / evec.two_norm();
      }
    proj = ( A * evec  );
    denom = inner_product( evec, evec );
    if( denom > 0 )
      {
      rayquo = inner_product( proj, proj  ) / denom; // - gradvec.two_norm() / gradvec.size() * 1.e2 ;
      }
    powerits++;
    if( rayquo > bestrayquo )
      {
      bestevec = evec;
      bestrayquo = rayquo;
      }
    else
      {
      relfac *= 0.9;
      evec = bestevec;
      }
    }
  evecin = bestevec;
  ::ants::antscout << "rayleigh-quotient: " << bestrayquo << " in " << powerits << " num " << maxorth << " fnz " << this->m_FractionNonZeroP << std::endl;
  return bestrayquo;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::RidgeCCA(unsigned int n_vecs)
{
  RealType taup = vnl_math_abs( this->m_FractionNonZeroP );
  RealType tauq = vnl_math_abs( this->m_FractionNonZeroQ );

  ::ants::antscout << " ridge cca : taup " << taup << " tauq " << tauq << std::endl;

  this->m_CanonicalCorrelations.set_size( n_vecs );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP, false );
  this->m_MatrixQ = this->NormalizeMatrix( this->m_OriginalMatrixQ, false );

  this->m_VariatesP.set_size( this->m_MatrixP.cols(), n_vecs );
  this->m_VariatesQ.set_size( this->m_MatrixQ.cols(), n_vecs );
  ::ants::antscout << " begin " << std::endl;
  unsigned int maxloop = this->m_MaximumNumberOfIterations;
  if( maxloop < 25 )
    {
    maxloop = 25;
    }
  RealType     totalcorr = 0;
  RealType     bestcorr = 0;
  unsigned int bestseed = 1;
  for( unsigned int seeder = 1; seeder < 100; seeder++ )
    {
    totalcorr = this->InitializeSCCA( n_vecs, seeder );
    if( totalcorr > bestcorr )
      {
      bestseed = seeder;  bestcorr = totalcorr;
      }
    totalcorr = 0;
    }
  ::ants::antscout << " Best initial corr " << bestcorr << std::endl;
  this->InitializeSCCA( n_vecs, bestseed );
  unsigned int loop = 0;
  bool         energyincreases = true;
  RealType     energy = 0;
  RealType     lastenergy = 0;
  while( ( ( loop < maxloop ) && ( energyincreases ) ) || loop < 3 )
    {
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType ptemp = this->m_VariatesP.get_column(k);
      VectorType qtemp = this->m_VariatesQ.get_column(k);
      VectorType pveck = this->m_MatrixQ * qtemp;
      VectorType qveck = this->m_MatrixP * ptemp;
      pveck = pveck * this->m_MatrixP;
      qveck = qveck * this->m_MatrixQ;
      for( unsigned int j = 0; j < k; j++ )
        {
        VectorType qj = this->m_VariatesP.get_column(j);  // this->ZeroProduct( pveck, qj );
        pveck = this->Orthogonalize( pveck, qj );
        qj = this->m_VariatesQ.get_column(j); // this->ZeroProduct( qveck, qj );
        qveck = this->Orthogonalize( qveck, qj );
        }
      pveck = pveck / pveck.two_norm();
      qveck = qveck / qveck.two_norm();
      pveck = ptemp + pveck * 0.01;
      qveck = qtemp + qveck * 0.01;
      this->m_VariatesP.set_column( k, pveck );
      this->m_VariatesQ.set_column( k, qveck );
      this->NormalizeWeightsByCovariance( k, taup, tauq );
      VectorType proj1 =  this->m_MatrixP * this->m_VariatesP.get_column( k );
      VectorType proj2 =  this->m_MatrixQ * this->m_VariatesQ.get_column( k );
      this->m_CanonicalCorrelations[k] = this->PearsonCorr( proj1, proj2  );
      }
    this->SortResults( n_vecs );
    lastenergy = energy;
    energy = this->m_CanonicalCorrelations.one_norm() / n_vecs;
    if ( ! m_Silent )
      {
      ::ants::antscout << " Loop " << loop << " Corrs : " << this->m_CanonicalCorrelations << " CorrMean : " << energy
                       << std::endl;
      }
    if( energy < lastenergy )
      {
      energyincreases = false;
      }
    else
      {
      energyincreases = true;
      }
    loop++;
    } // outer loop

  double ccasum = 0;
  for( unsigned int i = 0; i < this->m_CanonicalCorrelations.size(); i++ )
    {
    ccasum += fabs(this->m_CanonicalCorrelations[i]);
    }
  if( n_vecs > 1 )
    {
    return ccasum;
    }
  else
    {
    return fabs(this->m_CanonicalCorrelations[0]);
    }
  return this->m_CanonicalCorrelations[0];
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::RidgeRegression( typename antsSCCANObject<TInputImage,
                                            TRealType>::MatrixType& A,
                   typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
                   typename antsSCCANObject<TInputImage, TRealType>::VectorType  b,
                   TRealType lambda, unsigned int maxits, bool makesparse )
{
  MatrixType At = A.transpose();
  VectorType r_k = At * ( A * x_k );

  r_k = b - r_k - x_k * lambda;
  VectorType   p_k = r_k;
  double       approxerr = 1.e9;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = 1.e99;
  RealType     minerr = starterr, deltaminerr = 1, lasterr = starterr * 2;
  while(  deltaminerr > 1.e-4 && approxerr > 0 && ct < maxits )
    {
    RealType alpha_denom = inner_product( p_k, At * ( A * p_k ) );
    RealType iprk = inner_product( r_k, r_k );
    if( alpha_denom < 1.e-12 )
      {
      alpha_denom = 1;
      }
    RealType   alpha_k = iprk / alpha_denom;
    VectorType x_k1  = x_k + alpha_k * p_k; // this adds the scaled residual to the current solution
    x_k1  = x_k + alpha_k * p_k * .2;       // this adds the scaled residual to the current solution
    VectorType r_k1 =  b - At * (A * x_k1 ) - x_k1 * lambda;
    approxerr =  (b - At * (A * x_k1 ) ).two_norm() + x_k1.two_norm() * lambda;
    if( approxerr < minerr )
      {
      minerr = approxerr; bestsol = ( x_k1 );
      }
    deltaminerr = ( lasterr - approxerr );
    lasterr = approxerr;
    RealType bkdenom = inner_product( r_k, r_k );
    if( bkdenom < 1.e-12 )
      {
      bkdenom = 1;
      }
    RealType   beta_k = inner_product( r_k1, r_k1 ) / bkdenom; // classic cg
    VectorType p_k1  = r_k1 + beta_k * p_k;
    r_k = r_k1;
    p_k = p_k1;
    x_k = x_k1;
    ct++;
    }

  x_k = bestsol;
  if( makesparse )
    {
    this->SparsifyP( x_k );
    }
  return minerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::CGSPCA(unsigned int n_vecs )
{
  ::ants::antscout << " conjugate gradient sparse-pca approx to pca " << std::endl;
  std::vector<RealType> vexlist;

  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix( this->m_OriginalMatrixR );
    this->m_MatrixP = this->m_MatrixP - ( this->m_MatrixRRt * this->m_MatrixP );
    }
  MatrixType nspaceevecs = this->GetCovMatEigenvectors( this->m_MatrixP );
  VectorType nspaceevals = this->m_Eigenvalues;
  this->BasicSVD();
  MatrixType variatesInit = this->m_VariatesP;
  //  unsigned int n_vecs = this->m_VariatesP.cols();
  VectorType   evalInit( n_vecs );
  unsigned int evct = 0;
  for( unsigned int i = 0; i <  this->m_Eigenvalues.size();  i++ )
    {
    if( evct < n_vecs )
      {
      evalInit[evct] = this->m_Eigenvalues( i );
      }
    evct++;
    if( evct < n_vecs )
      {
      evalInit[evct] = this->m_Eigenvalues( i );
      }
    evct++;
    }
  unsigned int repspervec = this->m_MaximumNumberOfIterations;
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), repspervec * n_vecs );
  this->m_VariatesP.fill( 0 );
  RealType fnp = this->m_FractionNonZeroP;
  for(  unsigned int colind = 0; colind < n_vecs; colind++ )
    {
    ::ants::antscout << " colind " << colind << std::endl;
    for(  unsigned int whichevec = 0; whichevec < repspervec; whichevec++ )
      {
      VectorType   b = this->m_Eigenvectors.get_column( colind );
      unsigned int baseind = colind * repspervec;
      unsigned int lastbaseind = colind * repspervec;
      if( colind % 2 == 1 )
        {
        lastbaseind = ( colind - 1 ) * repspervec;
        }
      unsigned int locind = baseind + whichevec;
      VectorType   x_k = this->InitializeV( this->m_MatrixP, false );
      this->m_FractionNonZeroP = fnp + fnp * whichevec;
      RealType minerr1 = this->SparseNLConjGrad( this->m_MatrixP, x_k, b, 1.e-1, 10, true  ); // , 0 , colind );
      // //  , baseind ,
      // locind
      bool keepgoing = true;  unsigned int kgct = 0;
      while( keepgoing && kgct < 100 )
        {
        VectorType x_k2 = x_k;
        RealType   minerr2 = this->SparseNLConjGrad( this->m_MatrixP, x_k2, b, 1.e-1, 10, true ); // , 0 , colind
        // );
        keepgoing = false;
        // if ( fabs( minerr2 - minerr1 ) < 1.e-9 ) { x_k = x_k2; keepgoing = true; minerr1 = minerr2 ; }
        if( minerr2 < minerr1  )
          {
          x_k = x_k2; keepgoing = true; minerr1 = minerr2;
          }
        // ::ants::antscout << " minerr1 " << minerr1 << " wev " << whichevec << std::endl;
        kgct++;
        }
      for( unsigned int j = baseind; j < locind; j++ )
        {
        VectorType temp = this->m_VariatesP.get_column( j );
        this->ZeroProduct( x_k, temp );
        }
      this->m_VariatesP.set_column( locind, x_k  );
      } // repspervec
    }   // colind
  RealType reconstruction_error = this->ReconstructionError( this->m_MatrixP, this->m_VariatesP );
  ::ants::antscout << "%Var " << reconstruction_error <<  std::endl;
  for( unsigned int k = 0; k < this->m_VariatesP.cols(); k++ )
    {
    VectorType v = this->m_VariatesP.get_column( k );
    this->m_VariatesP.set_column( k, v / v.sum() );
    }
  this->m_Eigenvalues = this->m_CanonicalCorrelations = evalInit;
  return this->m_CanonicalCorrelations[0];
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::DeleteRow( typename antsSCCANObject<TInputImage,
                                      TRealType>::MatrixType& p_in,  unsigned int row )
{
  unsigned int nrows = p_in.rows() - 1;

  if( row >= nrows )
    {
    nrows = p_in.rows();
    }
  MatrixType   p( nrows, p_in.columns() );
  unsigned int rowct = 0;
  for( long i = 0; i < p.rows(); ++i )   // loop over rows
    {
    if( i != row )
      {
      p.set_row( rowct, p_in.get_row( i ) );
      rowct++;
      }
    }
  p_in = p;
  return;
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::LASSO_alg(  typename antsSCCANObject<TInputImage, TRealType>::MatrixType& X,
              typename antsSCCANObject<TInputImage, TRealType>::VectorType& y,
              typename antsSCCANObject<TInputImage, TRealType>::VectorType& beta_lasso, TRealType gamma,
              unsigned int maxits )
{
  VectorType   ypred;
  VectorType   yresid;
  RealType     besterr = 1.e20;
  RealType     curerr = besterr;
  unsigned int added = 0;

  for( unsigned int its = 0; its < maxits; its++ )
    {
    added = 0;
    ypred = X * beta_lasso;
    for( unsigned int j = 0; j < beta_lasso.size(); j++ )
      {
      VectorType xj = X.get_column( j );
      ypred = ypred - xj * beta_lasso( j );

      RealType regbeta = this->SimpleRegression( y, ypred );
      yresid = y - ( ypred * regbeta + this->m_Intercept );

      RealType beta = beta_lasso( j );
      for( unsigned int i = 0; i < xj.size(); i++ )
        {
        beta += ( xj( i ) * yresid( i ) );
        }
      beta = this->LASSOSoft( beta, gamma );
      ypred = ypred + xj * beta;
      regbeta = this->SimpleRegression( y, ypred );
      yresid = y - ( ypred * regbeta + this->m_Intercept );

      RealType sparseness = gamma * ( beta_lasso.one_norm() + fabs( beta ) - fabs( beta_lasso( j ) ) );
      curerr = yresid.two_norm() * 0.5 + sparseness;  /** Eq 7 Friedman pathwise */
      // ::ants::antscout << "Err: " << yresid.one_norm() * n  << " sp: " << sparseness << " cur " << curerr << " best "
      // << besterr << std::endl;
      if( curerr < besterr )
        {
        beta_lasso( j ) = beta;
        besterr = curerr;
        }
      else
        {
        ypred = ypred - xj * beta;
        ypred = ypred + xj * beta_lasso( j );
        }
      if( fabs( beta_lasso( j ) ) > 0 )
        {
        added++;
        }
      if( j % 1000 == 0 && false )
        {
        ::ants::antscout << "progress: " << 100 * j / X.cols() <<  " lasso-loop " << its << " : " <<  besterr
                         << " num_vars_added " << added << std::endl;
        }
      }
    //    ::ants::antscout <<  " lasso-iteration: " << its << " minerr: " <<  besterr << " num_vars_added: " << added
    //  << std::endl;
    } // iterations
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::LASSO_Cross()
{
  RealType gamma = this->m_FractionNonZeroP;

  ::ants::antscout << " Cross-validation - LASSO " << std::endl;

  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );
  this->m_MatrixR = this->NormalizeMatrix( this->m_OriginalMatrixR );
  if( this->m_OriginalMatrixR.size() <=  0 )
    {
    ::ants::antscout << " You need to define a reference matrix " << std::endl;
    std::exception();
    }
  VectorType   predictions( this->m_MatrixR.rows(), 0 );
  unsigned int foldnum = this->m_MaximumNumberOfIterations;
  if( foldnum <= 1 )
    {
    foldnum = 1;
    }
  VectorType folds( this->m_MatrixP.rows(), 0 );
  for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
    {
    folds( f ) = f % foldnum;
    }
  // Variates holds the average over all folds
  this->m_VariatesP( this->m_MatrixP.cols(), 1 );
  this->m_VariatesP.fill( 0 );
  this->m_VariatesQ = this->m_VariatesP;
  for( unsigned int fold = 0; fold < foldnum;  fold++ )
    {
    unsigned int foldct = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) == fold )
        {
        foldct++;
        }
      }
    MatrixType   p_leave_out( foldct, this->m_MatrixP.cols(), 0 );
    MatrixType   r_leave_out( foldct, this->m_MatrixR.cols(), 0 );
    unsigned int leftoutsize = this->m_MatrixP.rows() - foldct;
    MatrixType   matrixP( leftoutsize, this->m_MatrixP.cols() );
    MatrixType   matrixR( leftoutsize, this->m_MatrixR.cols() );
    unsigned int leave_out = 0;
    unsigned int dont_leave_out = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) == fold )
        {
        p_leave_out.set_row( leave_out, this->m_MatrixP.get_row( f ) );
        r_leave_out.set_row( leave_out, this->m_OriginalMatrixR.get_row( f ) );
        leave_out++;
        }
      else
        {
        matrixP.set_row( dont_leave_out, this->m_MatrixP.get_row( f ) );
        matrixR.set_row( dont_leave_out, this->m_MatrixR.get_row( f ) );
        dont_leave_out++;
        }
      }
    if( foldnum <= 1 )
      {
      matrixP = p_leave_out;
      matrixR = r_leave_out;
      }

    VectorType   rout = r_leave_out.get_column( 0 );
    VectorType   y =  matrixR.get_column( 0 );
    VectorType   yreal =  matrixR.get_column( 0 );
    unsigned int fleave_out = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) != fold )
        {
        yreal( fleave_out ) = this->m_OriginalMatrixR( f, 0 );
        fleave_out++;
        }
      }

    /**  train the lasso + regression model */
    VectorType beta_lasso(  matrixP.cols(), 0 );
    this->LASSO_alg( matrixP, y, beta_lasso, gamma, 2 );
    VectorType ypred = matrixP * beta_lasso;
    RealType   regbeta = this->SimpleRegression( yreal, ypred );
    RealType   intercept = yreal.mean() - ypred.mean() * regbeta;

    /**  test the lasso + regression model */
    VectorType localpredictions = p_leave_out * beta_lasso;
    localpredictions = localpredictions * regbeta + intercept;

    fleave_out = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) == fold )
        {
        predictions( f ) = localpredictions( fleave_out );
        fleave_out++;
        }
      }
    this->m_VariatesP.set_column( 0, this->m_VariatesP.get_column( 0 ) + beta_lasso * regbeta );
    ::ants::antscout << " fold " << fold << " mean-abs-error: "
                     << ( localpredictions - rout ).one_norm() / foldct << std::endl;
    }

  RealType regbeta = this->SimpleRegression( predictions, this->m_OriginalMatrixR.get_column( 0 )  );
  RealType intercept = this->m_OriginalMatrixR.get_column( 0 ).mean() - predictions.mean() * regbeta;
  RealType predictionerror =
    (  predictions * regbeta + intercept
       - this->m_OriginalMatrixR.get_column( 0 ) ).one_norm() / this->m_OriginalMatrixR.rows();
  ::ants::antscout << " overall-mean-abs-prediction-error: " << predictionerror << std::endl;
  this->m_Eigenvalues =  predictions * regbeta + intercept;
  this->m_CanonicalCorrelations =  predictions * regbeta + intercept;
  return predictionerror;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::LASSO( unsigned int n_vecs )
{
  /** Based on Friedman's pathwise coordinate optimization */
  RealType gamma = this->m_FractionNonZeroP;

  ::ants::antscout << " Pathwise LASSO : penalty = " << gamma << " veclimit = " << n_vecs << std::endl;

  this->m_CanonicalCorrelations.set_size( 1 );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );
  this->m_MatrixR = this->NormalizeMatrix( this->m_OriginalMatrixR );
  if( this->m_OriginalMatrixR.size() <=  0 )
    {
    ::ants::antscout << " You need to define a reference matrix " << std::endl;
    std::exception();
    }

  VectorType y = this->m_MatrixR.get_column( 0 );
  RealType   n = 1.0 / static_cast<RealType>( y.size() );
  VectorType beta_lasso( this->m_MatrixP.cols(), 0 );
  for( unsigned int i = 0; i < 1; i++ )
    {
    ::ants::antscout << i << std::endl;
    this->LASSO_alg( this->m_MatrixP, y, beta_lasso, gamma, this->m_MaximumNumberOfIterations );
    //    RealType spgoal =  ( RealType ) n_vecs ;
    // this->CurvatureSparseness( beta_lasso , spgoal , 100 );
    }
  /** post-process the result to select the largest n_vecs entries
  std::vector<TRealType> post( beta_lasso.size() , 0 );
  for( unsigned long  j = 0; j < beta_lasso.size(); ++j ) post[ j ] = fabs( beta_lasso( j ) );
  // sort and reindex the values
  sort( post.begin(), post.end(), my_sccan_sort_object);
  RealType thresh = 0;
  for( unsigned long j = 0; ( ( j < beta_lasso.size() ) && ( j < n_vecs ) ); ++j )
    {
    thresh = post[j];
    }
  unsigned int added = 0;
  if ( thresh > 0 )
    {
    added = 0;
    for(  unsigned long j = 0; j < beta_lasso.size(); ++j )
      {
      if ( fabs( beta_lasso( j ) ) < thresh ) beta_lasso( j ) = 0;
      else added++;
      }
    }
*/
  /** now get the original y value and result to find prediction error in true units */
  y = this->m_OriginalMatrixR.get_column( 0 );
  this->m_Intercept = this->ComputeIntercept(  this->m_MatrixP, beta_lasso, y );
  VectorType ypred = this->m_MatrixP * beta_lasso + this->m_Intercept;
  VectorType yresid = y - ( ypred  );
  this->m_CanonicalCorrelations( 0 ) = yresid.one_norm() * n;
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), 1 );
  this->m_VariatesP.set_column( 0,  beta_lasso  );
  ::ants::antscout << "final error " << this->m_CanonicalCorrelations( 0 ) <<  std::endl;
  return this->m_CanonicalCorrelations( 0 );
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseConjGradRidgeRegression( typename antsSCCANObject<TInputImage,
                                                          TRealType>::MatrixType& A,
                                 typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
                                 typename antsSCCANObject<TInputImage, TRealType>::VectorType  b_in, TRealType convcrit,
                                 unsigned int maxits, bool makesparse )
{
  x_k.fill( 0 );
  RealType spgoal = 100.0 * ( 1 - vnl_math_abs( this->m_FractionNonZeroP ) );
  RealType lambda = 1.e2;
  ::ants::antscout << "SparseConjGradRidgeRegression: lambda " << lambda << " Soft? " <<   this->m_UseL1 << " fnp "
                   << this->m_FractionNonZeroP << std::endl;
  MatrixType At = A.transpose();
  VectorType b( b_in );
  /** The data term b = b_n * A may be noisy.  here we optionally add a penalty
   *    \kappa \| \nabla b \|^2
   *   which smooths the data term and eases optimization
   */
  this->CurvatureSparseness( b, spgoal, 10 , this->m_MaskImageP );
  VectorType r_k = At * ( A * x_k );
  /** this is the gradient of the objective :
   *    \| b -  A^T A x \|  + \lambda \| x \|^2
   */
  r_k = b - r_k - x_k * lambda;
  VectorType   p_k = r_k;
  double       approxerr = 1.e9;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = 1.e99;
  RealType     minerr = starterr, deltaminerr = 1, lasterr = starterr * 2;
  if( makesparse )
    {
    this->SparsifyP( x_k  );
    }
  /** Here, should be more careful to make sure energy improves, need better command line interface for specifying
    objective */
  while( ( ( deltaminerr > 0.1 ) && ( approxerr > convcrit ) && ( ct < maxits ) )
         || ( ct < 3 ) )
    {
    /** standard conjugate gradient defintion of \alpha_k and p_k
     *  see http://www.matematicas.unam.mx/gfgf/cg2010/HISTORY-conjugategradient.pdf
     */
    RealType alpha_denom = inner_product( p_k, At * ( A * p_k ) );
    RealType iprk = inner_product( r_k, r_k );
    if( alpha_denom < 1.e-12 )
      {
      alpha_denom = 1;
      }
    /** A line search on the objective term , given search direction p_k
     *  see http://en.wikipedia.org/wiki/Nonlinear_conjugate_gradient_method
     */
    RealType   alpha_k = iprk / alpha_denom;
    VectorType x_k1  = x_k + alpha_k * p_k;
    /** To be careful (not necessary) orthogonalize with previous solutions
    for( unsigned int col = 0; col < this->m_VariatesP.cols(); col++ )
      {
      x_k1 = this->Orthogonalize( x_k1, this->m_VariatesP.get_column( col ) );
      }
    */
    // VectorType kvec( x_k1 );
    // RealType gradnorm = this->ComputeVectorGradMag( x_k1 , this->m_MaskImageP ).two_norm() ;
    this->CurvatureSparseness( x_k1, spgoal, 5, this->m_MaskImageP  );
    //    VectorType gradvec = this->ComputeVectorLaplacian( x_k1 , this->m_MaskImageP );
    if( makesparse )
      {
      /** use a hard thresholding on the update to enforce l_0 */
      this->SparsifyP( x_k1  );
      }
    RealType dataterm =  (b - At * (A * x_k1 ) ).two_norm();
    // VectorType r_k1 =  b - At * (A * x_k1 ) + gradvec * lambda;
    // approxerr = dataterm + gradnorm * lambda;
    /** compute the new gradient term */
    VectorType r_k1 =  b - At * (A * x_k1 ) - x_k1 * lambda; // ridge
    approxerr = dataterm + x_k1.two_norm() * lambda;
    if( approxerr < minerr )
      {
      minerr = approxerr; bestsol = ( x_k1 );
      }
    if( approxerr > minerr || ct % 20 == 0  )
      {
      this->m_GoldenSectionCounter = 0; RealType tau = 0.00001;
      RealType                                   loa = alpha_k * 0.25;
      RealType                                   hia = alpha_k * 1.e10;
      RealType                                   mida = ( loa + hia ) * 0.5;
      alpha_k = this->GoldenSection( A, x_k, p_k, b, loa, mida, hia, tau, lambda);
      x_k1  = x_k + alpha_k * p_k;
      this->CurvatureSparseness( x_k1, spgoal, 5, this->m_MaskImageP  );
      this->SparsifyP( x_k1 );
      dataterm =  (b - At * (A * x_k1 ) ).two_norm();
      r_k1 =  b - At * (A * x_k1 ) - x_k1 * lambda; // ridge
      approxerr = dataterm + x_k1.two_norm() * lambda;
      if( approxerr < minerr )
        {
        minerr = approxerr; bestsol = ( x_k1 );
        }
      }
    deltaminerr = ( lasterr - approxerr );
    ::ants::antscout << " SparseRidgeConjGrad " << approxerr <<  " deltaminerr " << deltaminerr <<  " ct " << ct
                     << " dataterm " << dataterm << std::endl;
    lasterr = approxerr;
    /** compute the new conjugate gradient term using standard approaches */
    RealType bkdenom = inner_product( r_k, r_k );
    if( bkdenom < 1.e-12 )
      {
      bkdenom = 1;
      }
    RealType   beta_k = inner_product( r_k1, r_k1 ) / bkdenom; // classic cg
    VectorType p_k1  = r_k1 + beta_k * p_k;
    r_k = r_k1;
    p_k = p_k1;
    x_k = x_k1;
    ct++;
    }

  return minerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::IHTRegression( typename antsSCCANObject<TInputImage,
                                          TRealType>::MatrixType& A,
                 typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k,
                 typename antsSCCANObject<TInputImage, TRealType>::VectorType  b_in, TRealType itkNotUsed( convcrit ),
                 unsigned int maxits, TRealType mu,  bool isp, bool verbose )
{
  if( b_in.size() != x_k.size() )
    {
    ::ants::antscout << " ITHRegression error --- b_in is the wrong size " << std::endl;
    return 1.e9;
    }
  vnl_diag_matrix<TRealType> mudiag( A.cols(), mu );
  RealType                   lambda = 0.e2;
  MatrixType                 At = A.transpose();
  VectorType                 b( b_in );
  VectorType                 p_k = At * ( A * x_k );
  p_k = b - p_k - x_k * lambda;
  VectorType   curdir = p_k;
  VectorType   lastdir = p_k;
  double       approxerr = 1.e9;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = 1.e99;
  RealType     minerr = starterr, deltaminerr = 1, lasterr = starterr * 2;
  if( isp )
    {
    this->SparsifyP( x_k );
    }
  else
    {
    this->SparsifyQ( x_k );
    }
  RealType alpha_k = 1.e-4 / A.frobenius_norm();
  if( verbose )
    {
    ::ants::antscout << " alpha_k " << alpha_k << " xknorm " << x_k.two_norm() << " Anorm " << A.frobenius_norm()
                     << std::endl;
    }
  RealType   gamma = 0;
  bool       conjgrad = true;
  VectorType lastgrad = x_k;
  //  while( ( ( deltaminerr > 0.1 ) && ( approxerr > convcrit ) && ( ct < maxits ) )
  //         || ( ct < 4 ) )
  while(  ( ct < maxits ) && alpha_k > 1.e-14 )
    {
    if( ( lastgrad.two_norm() > 0  ) && ( conjgrad )  && ( ct > 2 ) )
      {
      gamma = inner_product( p_k, p_k ) / inner_product( lastgrad, lastgrad );
      }
    lastdir = curdir;
    curdir = ( p_k + gamma * lastdir );
    if( !conjgrad )
      {
      curdir = p_k;
      }
    VectorType x_k1  = x_k + alpha_k * curdir;
    for ( unsigned int vp = 0; vp < this->m_VariatesP.cols(); vp++ )
      x_k1 = this->Orthogonalize( x_k1, this->m_VariatesP.get_column( vp ) );
    VectorType gradvec;
    if( isp )
      {
	//      x_k1 = this->SpatiallySmoothVector(  x_k1, this->m_MaskImageP );
      this->SparsifyP( x_k1 );
      gradvec = this->ComputeVectorLaplacian( x_k1, this->m_MaskImageP );
      }
    else
      {
	//      x_k1 = this->SpatiallySmoothVector(  x_k1, this->m_MaskImageQ );
      this->SparsifyQ( x_k1 );
      gradvec = this->ComputeVectorLaplacian( x_k1, this->m_MaskImageQ );
      }
    VectorType update = At * (A * x_k1 ) - mudiag * x_k1;
    RealType   dataterm =  (b - update ).two_norm();
    lastgrad = p_k;
    p_k =  b - update - x_k1 * lambda + gradvec * 0.e4; // ridge
    approxerr = dataterm + x_k1.two_norm() * lambda;
    if( approxerr < minerr )
      {
      minerr = approxerr; bestsol = ( x_k1 );
      }
    else
      {
      alpha_k *= 0.5;
      }
    deltaminerr = ( lasterr - approxerr );
    if( verbose )
      {
      ::ants::antscout << " IHTRegression " << approxerr <<  " deltaminerr " << deltaminerr <<  " ct " << ct
                       << " dataterm " << dataterm <<  " alpha_k " << alpha_k << std::endl;
      }
    lasterr = approxerr;
    x_k = x_k1;
    ct++;
    }

  return minerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::MatchingPursuit( typename antsSCCANObject<TInputImage,
                                            TRealType>::MatrixType& A,
                   typename antsSCCANObject<TInputImage, TRealType>::VectorType& x_k, TRealType convcrit,
                   unsigned int maxits )
{
  double       approxerr = 1.e9;
  unsigned int ct = 0;
  VectorType   bestsol = x_k;
  RealType     starterr = 1.e99;
  RealType     minerr = starterr, deltaminerr = 1, lasterr = starterr * 2;

  this->m_Indicator.set_size( A.cols() );
  this->m_Indicator.fill( 0 );
  MatrixType Ai;
  VectorType resid = this->m_OriginalB;
  while( ( ( deltaminerr > 1.e-3 ) && ( approxerr > convcrit ) && ( ct < maxits ) ) || ( ct < 10 ) )
    {
    VectorType corrb( x_k );
    for( unsigned int mm = 0; mm < A.cols(); mm++ )
      {
      corrb( mm ) = vnl_math_abs( this->PearsonCorr( resid, A.get_column( mm ) ) );
      }
    this->ReSoftThreshold( corrb, vnl_math_abs( this->m_FractionNonZeroP ), true );

    /** this debiases the solution */
    unsigned int nzct = 0;
    for( unsigned int mm = 0; mm < A.cols(); mm++ )
      {
      if( vnl_math_abs( corrb( mm ) ) > 0 )
        {
        this->m_Indicator( mm, mm ) = 1;
        }
      }
    this->GetSubMatrix( A, Ai );
    VectorType lmsolv = this->InitializeV( Ai, true );
    (void) this->ConjGrad( Ai, lmsolv, resid, 0, 10000 );
    x_k.fill( 0 );
    for( unsigned int mm = 0; mm < A.cols(); mm++ )
      {
      if( this->m_Indicator( mm, mm ) > 0 )
        {
        x_k( mm ) = lmsolv( nzct ); nzct++;
        }
      }
    this->ReSoftThreshold( x_k, vnl_math_abs( this->m_FractionNonZeroP ), true );
    this->ClusterThresholdVariate( x_k, this->m_MaskImageP, this->m_MinClusterSizeP );

    this->m_Intercept = this->ComputeIntercept(  A, x_k, this->m_OriginalB );
    VectorType soln = A * x_k + this->m_Intercept;
    resid = ( this->m_OriginalB - soln );
    approxerr = resid.one_norm();
    if( approxerr < minerr )
      {
      minerr = approxerr; bestsol = ( x_k );
      }
    deltaminerr = ( lasterr - approxerr );
    ::ants::antscout << " MatchingPursuit " << approxerr <<  " deltaminerr " << deltaminerr <<  " ct " << ct
                     << " diag " << this->m_Indicator.diagonal().sum() << std::endl;
    lasterr = approxerr;
    ct++;
    }

  return minerr;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::NetworkDecomposition(unsigned int n_vecs )
{
  //  MatrixType rmat = this->m_OriginalMatrixR.extract(
  //  this->m_OriginalMatrixR.rows() , this->m_OriginalMatrixR.cols() - 1 , 0 , 1 );
  // rmat = this->NormalizeMatrix( rmat );
  /** Based on Golub CONJUGATE  G R A D I E N T   A N D  LANCZOS  HISTORY
   *  http://www.matematicas.unam.mx/gfgf/cg2010/HISTORY-conjugategradient.pdf
   */
  ::ants::antscout << " network decomposition using nlcg & normal equations " << std::endl;

  this->m_CanonicalCorrelations.set_size( this->m_OriginalMatrixP.rows() );
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP );
  this->m_MatrixR = this->NormalizeMatrix( this->m_OriginalMatrixR );
  this->m_MatrixR = this->m_OriginalMatrixR;
  if( this->m_OriginalMatrixR.size() <=  0 )
    {
    ::ants::antscout << " You need to define a reference matrix " << std::endl;
    std::exception();
    }

  unsigned int foldnum = this->m_MaximumNumberOfIterations;
  if( foldnum <= 1 )
    {
    foldnum = 1;
    }
  VectorType folds( this->m_MatrixP.rows(), 0 );
  for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
    {
    folds( f ) = f % foldnum;
    }
  RealType     avgprederr = 0.0;
  RealType     avgfiterr = 0.0;
  unsigned int extra_cols = 0;
  // VariatesQ holds the average over all folds
  this->m_VariatesQ.set_size( this->m_MatrixP.cols(), n_vecs + extra_cols );
  this->m_VariatesQ.fill( 0 );
  RealType     totalloerror = 0;
  RealType     loerror = 0;
  unsigned int predct = 0;
  for( unsigned int fold = 0; fold < foldnum;  fold++ )
    {
    unsigned int foldct = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) == fold )
        {
        foldct++;
        }
      }
    MatrixType   p_leave_out( foldct, this->m_MatrixP.cols(), 0 );
    MatrixType   r_leave_out( foldct, this->m_MatrixR.cols(), 0 );
    unsigned int leftoutsize = this->m_MatrixP.rows() - foldct;
    MatrixType   matrixP( leftoutsize, this->m_MatrixP.cols() );
    MatrixType   matrixR( leftoutsize, this->m_MatrixR.cols() );
    unsigned int leave_out = 0;
    unsigned int dont_leave_out = 0;
    for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
      {
      if( folds( f ) == fold )
        {
        p_leave_out.set_row( leave_out, this->m_MatrixP.get_row( f ) );
        r_leave_out.set_row( leave_out, this->m_MatrixR.get_row( f ) );
        leave_out++;
        }
      else
        {
        matrixP.set_row( dont_leave_out, this->m_MatrixP.get_row( f ) );
        matrixR.set_row( dont_leave_out, this->m_MatrixR.get_row( f ) );
        dont_leave_out++;
        }
      }
    if( foldnum <= 1 )
      {
      matrixP = p_leave_out;
      matrixR = r_leave_out;
      }

    if( matrixR.cols() > 1 && false )
      {
      MatrixType m( matrixR.rows(), matrixR.cols() - 1, 0 );
      for( unsigned int mm = 1; mm < matrixR.cols(); mm++ )
        {
        m.set_row( mm, matrixR.get_column( mm ) );
        }
      MatrixType projmat = this->ProjectionMatrix( m, 1.e-2 );
      matrixP = matrixP - projmat * matrixP;

      MatrixType m2( r_leave_out.rows(), r_leave_out.cols() - 1, 0 );
      for( unsigned int mm = 1; mm < r_leave_out.cols(); mm++ )
        {
        m2.set_row( mm, r_leave_out.get_column( mm ) );
        }
      projmat = this->ProjectionMatrix( m2, 1.e-2 );
      p_leave_out = p_leave_out - projmat * p_leave_out;
      }

    VectorType soln;
    MatrixType A;
    this->m_VariatesP.set_size( matrixP.cols(), n_vecs + extra_cols );
    this->m_VariatesP.fill( 0 );
    VectorType intercept( matrixP.cols(), 1 );
    if( extra_cols > 0 )
      {
      this->m_VariatesP.set_column( 0, intercept );
      }
    VectorType   original_b =  matrixR.get_column( 0 );
    unsigned int colind = extra_cols;
    RealType     minerr1;
    bool         addcol = false;
    matrixP = this->NormalizeMatrix( matrixP );
    while(  colind < this->m_VariatesP.cols()  )
      {
      VectorType b =  original_b;  this->m_OriginalB = original_b;
      VectorType x_k = this->m_VariatesP.get_column( colind );
      /***************************************/
      if( colind > 0 )
        {
        A = matrixP * this->m_VariatesP.extract( matrixP.cols(), colind, 0, 0);
        if( addcol )
          {
          this->AddColumnsToMatrix( A, matrixR, 1, this->m_MatrixR.cols() - 1 );
          }
        VectorType lmsolv( A.cols(), 1 );
        (void) this->ConjGrad( A, lmsolv, original_b, 0, 10000 );
        VectorType v = ( A * lmsolv + this->m_Intercept );
        b = this->Orthogonalize( b, v );
        }
      unsigned int adder = 0;
      b = b - b.mean();
      for( unsigned int cl = colind; cl < colind + 2; cl++ )
        {
        VectorType randv = this->InitializeV( matrixP, 0 );
        VectorType bp = b * matrixP;
        VectorType bpneg( bp );
	this->PosNegVector( bp, true );
        this->PosNegVector( bpneg, false );
        if( cl % 2 != 0 )
          {
          bp = bpneg * (-1);
          }
        // minerr1 = this->SparseNLPreConjGrad( matrixP, randv, bp, 1.e-1, 100 ); /**/
        // minerr1 = this->SparseNLConjGrad( matrixP, randv, bp, 1.e-5, 500, true , true  ); /** miccai good? */
        // minerr1 = this->MatchingPursuit(  matrixP ,  randv,  0, 90  );/** bad */
        // minerr1 = this->SparseConjGradRidgeRegression(  matrixP,  randv, bp, 0, 150, true );/** good */
        //	randv.fill( 0 );
        // minerr1 = this->RidgeRegression(  matrixP ,  randv, bp, 1.e4 , 100 , false );/** biased but ok */
        // minerr1 = this->ConjGrad(  matrixP ,  randv, matrixP * bp , 0, 10000 );      this->SparsifyP( randv );
        minerr1 = this->IHTRegression(  matrixP,  randv, bp, 0, 500, 0, true, true ); /** good */
        if( cl < this->m_VariatesP.cols() )
          {
          this->m_VariatesP.set_column( cl, randv );
          }
        adder++;
        ::ants::antscout << " cl " << cl << " bp mean " << bp.mean() << " randvmean " << randv.mean() << std::endl;
        }
      colind = colind + adder;
      /***************************************/
      /* Training : Now get the LSQ regression solution */
      /***************************************/
      A = matrixP * this->m_VariatesP.extract( matrixP.cols(), colind, 0, 0);
      if( addcol )
        {
        this->AddColumnsToMatrix( A, matrixR, 1, this->m_MatrixR.cols() - 1 );
        }
      VectorType lmsolv( A.cols(), 1 );
      ( void ) this->ConjGrad(  A,  lmsolv, original_b, 0, 10000 );
      this->m_Intercept = this->ComputeIntercept( A, lmsolv,  original_b );
      soln = A * lmsolv + this->m_Intercept;
      RealType fiterr = ( original_b - soln ).one_norm() / original_b.size();
      avgfiterr += ( fiterr * ( 1.0 / ( RealType ) foldnum ) );
      /** Testing */
      p_leave_out = this->NormalizeMatrix( p_leave_out );
      A = p_leave_out * this->m_VariatesP.extract( matrixP.cols(), colind, 0, 0);
      if( addcol )
        {
        this->AddColumnsToMatrix( A, r_leave_out, 1, this->m_MatrixR.cols() - 1 );
        }
      soln = ( A * lmsolv ) + this->m_Intercept;
      loerror = ( soln - r_leave_out.get_column( 0 ) ).one_norm() / soln.size();
      unsigned int fleave_out = 0;
      if( foldnum == 1 )
        {
        this->m_CanonicalCorrelations = soln;
        }
      else
        {
        for( unsigned int f = 0; f < this->m_MatrixP.rows(); f++ )
          {
          if( folds( f ) == fold )
            {
            this->m_CanonicalCorrelations( f ) = soln( fleave_out );
            RealType temp = fabs( soln( fleave_out ) - r_leave_out.get_column( 0 ) ( fleave_out ) );
            if( colind == this->m_VariatesP.cols() )
              {
              avgprederr += temp; predct++;
              }
            fleave_out++;
            }
          }
        }
      if( predct > 0 )
        {
        ::ants::antscout << "Fold: " << fold << " minerr," << loerror << ",col," << colind
                         << " totalpredictionerr " << avgprederr / predct <<  " ,fiterr, " << fiterr << " Gap: "
                         <<  ( avgprederr / predct ) - fiterr  << std::endl;
        }
      else
        {
        ::ants::antscout << "Fold: " << fold << " minerr," << loerror << ",col," << colind  << " ,fiterr, "
                         << fiterr << std::endl;
        }
      }

    RealType wt =
      ( ( soln
          - r_leave_out.get_column( 0 ) ).one_norm()
        / soln.size() ) / (  ( r_leave_out.get_column( 0 ) ).one_norm() / soln.size() );
    this->m_VariatesQ = this->m_VariatesQ + this->m_VariatesP * ( 1 / wt );
    totalloerror += ( 1 / wt );
    }
  this->m_VariatesQ = this->m_VariatesQ / totalloerror;
  RealType regbeta = this->SimpleRegression(  this->m_CanonicalCorrelations, this->m_OriginalMatrixR.get_column( 0 )  );
  RealType intercept = this->m_OriginalMatrixR.get_column( 0 ).mean() - this->m_CanonicalCorrelations.mean()
    * regbeta;
  VectorType predicted = this->m_CanonicalCorrelations * regbeta + intercept;
  RealType   predictionerror =
    (  predicted
       - this->m_OriginalMatrixR.get_column( 0 ) ).one_norm() / this->m_OriginalMatrixR.rows();
  ::ants::antscout << " overall-mean-abs-prediction-error: " << predictionerror << " correlation "
                   <<  this->PearsonCorr(  predicted,
                          this->m_OriginalMatrixR.get_column( 0 ) )  << " Gap " <<  avgprederr / predct - avgfiterr
                   << std::endl;
  this->m_VariatesP = this->m_VariatesQ;
  for( unsigned int i = 0; i < this->m_VariatesP.cols(); i++ )
    {
    VectorType col = this->m_VariatesP.get_column( i );
    this->SparsifyP( col  ); // make averaged predictors sparse!
    this->m_VariatesP.set_column( i, col );
    }

  return avgprederr / predct;
}

/*

    MatrixType pmod = this->m_MatrixP;
    this->m_Indicator.set_size(this->m_MatrixP.cols());
    this->m_Indicator.fill(1);
    if ( whichevec > 0 && false )
      {
      // zero out already used parts of matrix
      for ( unsigned int mm = baseind; mm < locind; mm++ )
        {
        VectorType u =  this->m_VariatesP.get_column( mm );
        for ( unsigned int j=0; j< u.size(); j++)
          if ( fabs(u(j)) > 0 ) this->m_Indicator( j , j ) = 0;
        }
      pmod = pmod * this->m_Indicator;
      b = b * this->m_Indicator;
      }

    if( whichevec > 0 && false )
      {
      MatrixType m( this->m_MatrixP.rows(), locind - baseind , 0 );
      for( unsigned int mm = baseind; mm < locind; mm++ )
  {
        m.set_row( mm, this->m_MatrixP * this->m_VariatesP.get_column( mm ) );
  }
      MatrixType projmat = this->ProjectionMatrix( m, 1.e-2 );
      pmod = pmod - projmat * pmod;
      }



    if ( whichevec > 0 )
    {
    MatrixType A = this->m_MatrixP * this->m_VariatesP.get_n_columns( baseind , whichevec  );
    VectorType lmsolv( A.cols() , 1 );
    VectorType bproj = this->m_MatrixP * b;
    this->ConjGrad(  A ,  lmsolv, bproj , 0, 10000 );
    bproj = bproj - A * lmsolv;
    bp = bproj * this->m_MatrixP;
    for( unsigned int vv = 0; vv < bp.size(); vv++ )
      if ( colind % 2 == 0 & bp( vv ) > 0 ) bp( vv ) = 0;
      else if ( colind % 2 > 0 & bp( vv ) < 0 ) bp( vv ) = 0;
    }

    VectorType bnspace = this->m_MatrixP * this->m_Eigenvectors.get_column( colind );
    ::ants::antscout << " vecerr-a " << this->PearsonCorr( this->m_MatrixP * x_k , bnspace )<< " norm " << ( this->m_MatrixP * x_k - bnspace ).two_norm() << std::endl;
    ::ants::antscout << " vecerr-a-init " <<  this->PearsonCorr( this->m_MatrixP * variatesInit.get_column( colind ) , bnspace ) << " norm " <<  ( this->m_MatrixP * variatesInit.get_column( colind ) - bnspace ).two_norm() << std::endl;
    ::ants::antscout << " col " << colind << " of  " <<  n_vecs << " nspaceevecssz " << nspaceevecs.cols() << " mnv " << x_k.min_value() << " mxv " << x_k.max_value() << " colind " << colind << std::endl;

    this->m_Indicator.set_size(this->m_MatrixP.cols());
    this->m_Indicator.fill(1);
    if ( colind > 0)
      {
      // zero out already used parts of matrix
      for ( unsigned int mm = 0; mm < colind; mm++ )
        {
        VectorType u =  this->m_VariatesP.get_column( mm );
        for ( unsigned int j=0; j< u.size(); j++)
          if ( fabs(u(j)) >= this->m_Epsilon ) this->m_Indicator( j , j ) = 0;
        }
      //      pmod = pmod * this->m_Indicator;
      }
*/

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::BasicSVD()
{
  unsigned int n_vecs = this->m_MatrixP.rows() - 1;

  if(  ( this->m_MatrixP.cols() - 1 ) < n_vecs )
    {
    n_vecs = this->m_MatrixP.cols() - 1;
    }
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  ::ants::antscout << " basic svd " << std::endl;
  std::vector<RealType> vexlist;
  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
    }
  this->m_VariatesP.set_size( this->m_MatrixP.cols(), n_vecs * 2 );

  MatrixType init = this->GetCovMatEigenvectors( this->m_MatrixP );
  ::ants::antscout << "got initial svd " << std::endl;
  m_Eigenvectors.set_size( this->m_MatrixP.cols(), n_vecs * 2 );
  unsigned int svdct = 0;
  RealType     fnp = this->m_FractionNonZeroP;
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    if( kk < init.columns() )
      {
      VectorType initvpos = init.get_column(kk) * this->m_MatrixP;
      VectorType initvneg = init.get_column(kk) * this->m_MatrixP;
      if( true )
        {
        for( unsigned int vv = 0; vv < initvneg.size(); vv++ )
          {
          if( initvneg( vv ) > 0 )
            {
            initvneg( vv ) = 0;
            }
          else
            {
            initvneg( vv ) = fabs( initvneg( vv ) );
            }
          if( initvpos( vv ) < 0 )
            {
            initvpos( vv ) = 0;
            }
          }
        m_Eigenvectors.set_column( svdct,  initvneg * (-1)  );
        if( fnp < 1 && false )
          {
          if( this->m_KeepPositiveP )
            {
            this->ConstantProbabilityThreshold( initvneg, fnp, this->m_KeepPositiveP );
            }
          else
            {
            this->ReSoftThreshold( initvneg, fnp, !this->m_KeepPositiveP );
            }
          this->ClusterThresholdVariate( initvneg, this->m_MaskImageP, this->m_MinClusterSizeP );
          }
        this->m_VariatesP.set_column( svdct,   initvneg * (-1) );
        svdct++;
        m_Eigenvectors.set_column( svdct, initvpos  );
        if( fnp < 1  && false )
          {
          if( this->m_KeepPositiveP )
            {
            this->ConstantProbabilityThreshold( initvpos, fnp, this->m_KeepPositiveP );
            }
          else
            {
            this->ReSoftThreshold( initvpos, fnp, !this->m_KeepPositiveP );
            }
          this->ClusterThresholdVariate( initvpos, this->m_MaskImageP, this->m_MinClusterSizeP );
          }
        this->m_VariatesP.set_column( svdct, initvpos );
        } // separate the eigenvectors into + / -
      else
        {
        this->m_VariatesP.set_column( svdct, initvpos );
        }
      svdct++;
      }
    }
  //  double   ktrace = vnl_trace<double>(   this->m_MatrixP *  this->m_MatrixP.transpose()  );
  // RealType vex = this->ComputeSPCAEigenvalues( this->m_VariatesP.cols(), this->m_Eigenvalues.sum(), true );
  // ::ants::antscout << "original-vex : " << this->m_Eigenvalues.sum() / ktrace <<  " sparse-vex: " << vex <<
  // std::endl;
  return this->m_CanonicalCorrelations[0];
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparseArnoldiSVD(unsigned int n_vecs )
{
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  ::ants::antscout << " arnoldi sparse svd : cg " << std::endl;
  std::vector<RealType> vexlist;
  this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
  this->m_MatrixQ = this->m_MatrixP;
  if( this->m_OriginalMatrixR.size() > 0 )
    {
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
    }
  this->m_ClusterSizes.set_size(n_vecs);
  this->m_ClusterSizes.fill(0);
  double trace = 0;
  for( unsigned int i = 0; i < this->m_MatrixP.cols(); i++ )
    {
    trace += inner_product(this->m_MatrixP.get_column(i), this->m_MatrixP.get_column(i) );
    }
  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  VariateType myGradients;
  this->m_SparseVariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_SparseVariatesP.fill(0);
  myGradients.set_size(this->m_MatrixP.cols(), n_vecs);
  myGradients.fill(0);
  MatrixType init = this->GetCovMatEigenvectors( this->m_MatrixP );
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    this->m_VariatesP.set_column(kk, this->InitializeV(this->m_MatrixP) );
    if( kk < init.columns() )
      {
      VectorType initv = init.get_column(kk) * this->m_MatrixP;
      this->m_VariatesP.set_column(kk, initv);
      this->m_SparseVariatesP.set_column(kk, initv);
      }
    }
  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
// Arnoldi Iteration SVD/SPCA
  unsigned int loop = 0;
  bool         debug = false;
  double       convcrit = 1;
  RealType     fnp = 1;
  while( loop<maxloop && convcrit> 1.e-8 )
    {
    fnp = this->m_FractionNonZeroP;
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType pveck = this->m_SparseVariatesP.get_column(k);
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( pveck, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( pveck, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( pveck, this->m_MaskImageP, this->m_MinClusterSizeP );
        }
      //      for ( unsigned int i=0; i<k; i++) pveck = this->Orthogonalize(pveck, this->m_VariatesP.get_column(i) );
      RealType   alpha = 0.1;
      VectorType resid = init.get_column(k) - this->m_MatrixP * pveck;
      VectorType newsol = this->m_SparseVariatesP.get_column(k) +  this->m_MatrixP.transpose() * resid * alpha;
      this->m_SparseVariatesP.set_column(k, newsol);
      if( fnp < 1 )
        {
        if( this->m_KeepPositiveP )
          {
          this->ConstantProbabilityThreshold( newsol, fnp, this->m_KeepPositiveP );
          }
        else
          {
          this->ReSoftThreshold( newsol, fnp, !this->m_KeepPositiveP );
          }
        this->ClusterThresholdVariate( newsol, this->m_MaskImageP, this->m_MinClusterSizeP );
        }
      this->m_VariatesP.set_column(k, newsol);
      } // kloop

    this->m_VariatesQ = this->m_VariatesP;
    if( debug )
      {
      ::ants::antscout << " get evecs " << std::endl;
      }
    RealType vex = this->ComputeSPCAEigenvalues(n_vecs, trace, true);
    vexlist.push_back(   vex    );
    this->SortResults( n_vecs );
    convcrit = ( this->ComputeEnergySlope(vexlist, 5) );
    ::ants::antscout << "Iteration: " << loop << " Eigenval_0: " << this->m_CanonicalCorrelations[0]
                     << " Eigenval_1: "
                     << this->m_CanonicalCorrelations[1] << " Eigenval_N: "
                     << this->m_CanonicalCorrelations[n_vecs
                                     - 1] << " Sparseness: " << fnp  << " convergence-criterion: " << convcrit
                     <<  " vex " << vex << std::endl;
    loop++;
    if( debug )
      {
      ::ants::antscout << "wloopdone" << std::endl;
      }
    } // opt-loop
      //  ::ants::antscout << " cluster-sizes " << this->m_ClusterSizes << std::endl;
  for( unsigned int i = 0; i < vexlist.size(); i++ )
    {
    ::ants::antscout << vexlist[i] << ",";
    }
  ::ants::antscout << std::endl;
  return fabs(this->m_CanonicalCorrelations[0]);
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::ComputeSPCAEigenvalues(unsigned int n_vecs, TRealType trace, bool orth )
{
  this->m_CanonicalCorrelations.set_size( n_vecs );
  double evalsum = 0;
  //   we have   variates  P = X  ,  Q = X^T  ,    Cov \approx \sum_i eval_i E_i^t E_i
  //   where E_i - eigenvector,  eval_i eigenvalue
  unsigned long mind = this->m_MatrixP.rows();
  if( mind > this->m_MatrixP.cols() )
    {
    mind = this->m_MatrixP.cols();
    }
  // we estimate variance explained by  \sum_i eigenvalue_i / trace(A) ...
  // shen and huang
  //  MatrixType kcovmat=this->VNLPseudoInverse( this->m_VariatesP.transpose()*this->m_VariatesP
  // )*this->m_VariatesP.transpose();
  // kcovmat=(this->m_MatrixP*this->m_VariatesP)*kcovmat;
  // double ktrace=vnl_trace<double>(   kcovmat  );
  // ::ants::antscout <<" ktr  " << ktrace << std::endl;
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    VectorType u = this->m_VariatesP.get_column(i);
    //    vnl_diag_matrix<TRealType> indicator(this->m_MatrixP.cols(),1);
    //    for ( unsigned int j=0; j< u.size(); j++) if ( fabs(u(j)) <= this->m_Epsilon ) indicator(j,j)=0;
    VectorType proj = this->m_MatrixP.transpose() * ( this->m_MatrixP * u );
    double     eigenvalue_i = 0;
    double     p2n = proj.two_norm();
    double     oeig = p2n;
    proj =  this->m_MatrixP * u;
    eigenvalue_i = oeig;
    u = this->m_MatrixP * u;
    double unorm = u.two_norm();
    if( unorm >  0 )
      {
      u = u / u.two_norm();
      }
    // factor out the % of the eigenvector that is not orthogonal to higher ranked eigenvectors
    for( unsigned int j = 0; j < i; j++ )
      {
      VectorType v = this->m_MatrixP * this->m_VariatesP.get_column(j);
      if( v.two_norm() >  0 )
        {
        v = v / v.two_norm();
        }
      double ip =  inner_product( u, v );
      ip = 1 - fabs( ip );
      if( orth )
        {
        eigenvalue_i *= ip;
        }
      }
    if( eigenvalue_i == 0 )
      {
      this->m_VariatesP.set_column( i, this->InitializeV( this->m_MatrixP ) );
      }
    evalsum += oeig; // eigenvalue_i;
    this->m_CanonicalCorrelations[i] = eigenvalue_i;
    }
  evalsum /= trace;
  //  ::ants::antscout << this->m_CanonicalCorrelations << std::endl;
  double vex = this->m_CanonicalCorrelations.sum() / trace;
  //  ::ants::antscout<<" adjusted variance explained " << vex << std::endl;
  // ::ants::antscout<<" raw variance explained " <<  evalsum << std::endl;
  /*
  MatrixType mmm =  this->m_MatrixP *  this->m_VariatesP ;
  MatrixType projmat = this->ProjectionMatrix( mmm );
  projmat = projmat * this->m_MatrixP ;
  projmat = projmat * this->m_MatrixP.frobenius_norm() / projmat.frobenius_norm();
  MatrixType resid = this->m_MatrixP - projmat;
  double ktrace=vnl_trace<double>(   resid  );
  // if we factored out everything then ktrace will be small
  ::ants::antscout <<" ktr  " << ktrace << " trace " << this->m_Eigenvalues.sum() << std::endl;
  */
  return vex;
  /*****************************************/
  MatrixType ptemp(this->m_MatrixP);
  VectorType d_i(n_vecs, 0);
  for( unsigned int i = 0; i < n_vecs; i++ )
    {
    // find d_i
    MatrixType m = outer_product(this->m_VariatesQ.get_column(i), this->m_VariatesP.get_column(i) );
    // now find d_i to bring m close to ptemp
    RealType a = ptemp.frobenius_norm();
    RealType b = m.frobenius_norm();
    //    m=m*(a/b);
    RealType hypod = inner_product(this->m_VariatesQ.get_column(i), this->m_MatrixP * this->m_VariatesP.get_column(i) );
    ::ants::antscout << " hypod " << hypod << " a " << a << " b " << b << " a/b " << a / b << " " << std::endl;
    ptemp = ptemp + m * a;
    }
  return 0;
}

template <class TInputImage, class TRealType>
typename antsSCCANObject<TInputImage, TRealType>::MatrixType
antsSCCANObject<TInputImage, TRealType>
::GetCovMatEigenvectors( typename antsSCCANObject<TInputImage, TRealType>::MatrixType rin  )
{
  double     pinvTolerance = this->m_PinvTolerance;
  MatrixType dd = this->NormalizeMatrix(rin);
  MatrixType cov = dd * dd.transpose();

  cov.set_identity();
  TRealType regularization = 0;
  cov = cov * regularization + rin * rin.transpose();
  vnl_svd<RealType> eig(cov, pinvTolerance);
  VectorType        vec1 = eig.U().get_column(0);
  VectorType        vec2 = eig.V().get_column(0);
  double            evalsum = 0;
  this->m_Eigenvalues.set_size(cov.rows() );
  this->m_Eigenvalues.fill(0);
  for( unsigned int i = 0; i < cov.rows(); i++ )
    {
    this->m_Eigenvalues[i] = eig.W(i, i);
    evalsum += eig.W(i, i);
    //    ::ants::antscout <<" variance-explained-eval " << i << " = " << evalsum/trace*100  << std::endl;
    }
//  ::ants::antscout <<" W 1 " << eig.W(0,0) << " W 2 " << eig.W(1,1) << std::endl;
  if( vec2.size() == rin.rows() )
    {
    this->m_Eigenvectors = eig.V();
    return eig.V();
    }
  else
    {
    this->m_Eigenvectors = eig.U();
    return eig.U();
    }
}

template <class TInputImage, class TRealType>
bool antsSCCANObject<TInputImage, TRealType>
::CCAUpdate( unsigned int n_vecs, bool allowchange  , bool normbycov )
{
  this->m_Debug = false;
  bool changedgrad = false;

  for( unsigned int k = 0; k < n_vecs; k++ )
    {
    VectorType ptemp = this->m_VariatesP.get_column(k);
    VectorType qtemp = this->m_VariatesQ.get_column(k);
    VectorType pveck = this->m_MatrixQ * qtemp;
    VectorType qveck = this->m_MatrixP * ptemp;
    /** the gradient of     ( x X ,  y Y ) * ( x X , x X )^{-1}  * ( y Y  , y Y )^{-1}
     *  where we constrain ( x X , x X ) = ( y Y , y Y ) = 1
     */
    RealType ccafactor = inner_product( pveck, qveck ) * 0.5;
    pveck = pveck * this->m_MatrixP;
    pveck = pveck - this->m_MatrixP.transpose() * ( this->m_MatrixP * ptemp ) *  ccafactor;
    qveck = qveck * this->m_MatrixQ;
    qveck = qveck - this->m_MatrixQ.transpose() * ( this->m_MatrixQ * qtemp ) *  ccafactor;
    for( unsigned int j = 0; j < k; j++ )
      {
      VectorType qj = this->m_VariatesP.get_column( j );
      pveck = this->Orthogonalize( pveck, qj );
      if ( this->m_Covering ) this->ZeroProduct( qveck,  qj );
      qj = this->m_VariatesQ.get_column( j );
      qveck = this->Orthogonalize( qveck, qj );
      if ( this->m_Covering ) this->ZeroProduct( qveck,  qj );
      }
    RealType sclp = ( static_cast<RealType>( pveck.size() )  * this->m_FractionNonZeroP );
    RealType sclq = ( static_cast<RealType>( qveck.size() )  * this->m_FractionNonZeroQ );
    bool     genomics = false;
    if( genomics )
      {
      VectorType y = this->m_MatrixP * ptemp;
      this->LASSO_alg( this->m_MatrixQ, y, qveck, 1.e-3, 2 );
      }
    pveck = ptemp + pveck * ( this->m_GradStep / sclp );
    qveck = qtemp + qveck * ( this->m_GradStep / sclq );
    for( unsigned int j = 0; j < k; j++ )
      {
      VectorType qj = this->m_VariatesP.get_column( j );
      pveck = this->Orthogonalize( pveck / ( this->m_MatrixP * pveck ).two_norm()  , qj );
      if ( this->m_Covering ) this->ZeroProduct( qveck,  qj );
      qj = this->m_VariatesQ.get_column( j );
      qveck = this->Orthogonalize( qveck / ( this->m_MatrixQ * qveck ).two_norm()  , qj );
      if ( this->m_Covering ) this->ZeroProduct( qveck,  qj );
      }
    //    pveck = this->SpatiallySmoothVector( pveck, this->m_MaskImageP );
    //    qveck = this->SpatiallySmoothVector( qveck, this->m_MaskImageQ );
    if ( ( this->m_UseLongitudinalFormulation > 1.e-9 ) && ( pveck.size() == qveck.size() ) )
      {
      VectorType lpveck = pveck + ( qveck - pveck ) * this->m_UseLongitudinalFormulation;
      VectorType lqveck = qveck + ( pveck - qveck ) * this->m_UseLongitudinalFormulation;
      pveck = lpveck;
      qveck = lqveck;
      }
    this->SparsifyP( pveck );
    this->SparsifyQ( qveck );
    if ( this->m_UseLongitudinalFormulation > 1.e-9 )
      {
      if ( k == 0  && ( pveck.size() != qveck.size() ) ) 
        ::ants::antscout << "Cannot Use Longitudinal formulation if input matrix sizes dont match. ";
      else if ( k == 0 )
        ::ants::antscout << "Longitudinal=>p=q: " << ( pveck - qveck ).one_norm();
      }
    if( n_vecs == 0 )
      {
      RealType mup = inner_product( this->m_MatrixP * ptemp, this->m_MatrixP * ptemp ) / ptemp.two_norm();
      RealType muq = inner_product( this->m_MatrixQ * qtemp, this->m_MatrixQ * qtemp ) / qtemp.two_norm();
      ::ants::antscout << " USE-IHT FORMULATION " << mup << "  " << muq << std::endl;
      this->IHTRegression(  this->m_MatrixP,  ptemp, pveck, 0, 1, mup, true, false );   pveck = ptemp;
      this->IHTRegression(  this->m_MatrixQ,  qtemp, qveck, 0, 1, muq, false, false );   qveck = qtemp;
      }
    // test 4 cases of updates
    RealType corr0 = this->PearsonCorr( this->m_MatrixP * ptemp, this->m_MatrixQ * qtemp  );
    RealType corr1 = this->PearsonCorr( this->m_MatrixP * pveck, this->m_MatrixQ * qveck  );
    RealType corr2 = this->PearsonCorr( this->m_MatrixP * ptemp, this->m_MatrixQ * qveck  );
    RealType corr3 = this->PearsonCorr( this->m_MatrixP * pveck, this->m_MatrixQ * qtemp  );
    if( corr1 > corr0 )
      {
      this->m_VariatesP.set_column( k, pveck  );
      this->m_VariatesQ.set_column( k, qveck  );
      if( this->m_Debug )
        {
        ::ants::antscout << " corr1 " << corr1 << " v " << corr2 << std::endl;
        }
      }
    else if( ( corr2 > corr0 )  &&  ( corr2 > corr3 ) )
      {
      this->m_VariatesQ.set_column( k, qveck  );
      if( this->m_Debug )
        {
        ::ants::antscout << " corr2 " << corr2 << " v " << corr1 << std::endl;
        }
      }
    else if( corr3 > corr0 )
      {
      this->m_VariatesP.set_column( k, pveck  );
      if( this->m_Debug )
        {
        ::ants::antscout << " corr3 " << corr3 << " v " << corr0 << std::endl;
        }
      }
    else if( allowchange )
      {
      changedgrad = true;
      this->m_GradStep *= 0.9;
      if( this->m_Debug )
        {
        ::ants::antscout << " corr0 " << corr0 <<  " v " << corr1 << " NewGrad " << this->m_GradStep <<  std::endl;
        }
      }
    if ( normbycov ) this->NormalizeWeightsByCovariance( k, 0, 0 );
    else this->NormalizeWeights( k );
    VectorType proj1 =  this->m_MatrixP * this->m_VariatesP.get_column( k );
    VectorType proj2 =  this->m_MatrixQ * this->m_VariatesQ.get_column( k );
    this->m_CanonicalCorrelations[k] = this->PearsonCorr( proj1, proj2  );
    }
  this->SortResults( n_vecs );
  return this->m_CanonicalCorrelations.mean();
}


template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::InitializeSCCA_simple( unsigned int n_vecs )
{
  /** get the row mean for each matrix , then correlate the other matrix with that, then sparsify
   *  for 2nd,3rd,etc evecs, orthogonalize initial vector against sparsified.
   */
  RealType totalcorr = 0;
  VectorType prowmean( this->m_MatrixP.rows(), 0 );
  for( unsigned long i = 0; i < this->m_MatrixP.rows(); i++ )
    {
    prowmean( i ) = this->m_MatrixP.get_row( i ).mean();
    }
  VectorType qrowmean( this->m_MatrixQ.rows(), 0 );
  for( unsigned long i = 0; i < this->m_MatrixQ.rows(); i++ )
    {
    qrowmean( i ) = this->m_MatrixQ.get_row( i ).mean();
    }
  prowmean = prowmean / prowmean.two_norm();
  qrowmean = qrowmean / qrowmean.two_norm();
  VectorType ipvec = ( prowmean + qrowmean ) * this->m_MatrixP;
  VectorType iqvec = ( prowmean + qrowmean ) * this->m_MatrixQ;
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    VectorType qvec = ( this->m_MatrixP * ipvec ) * this->m_MatrixQ;
    qvec = qvec / qvec.two_norm();
    VectorType vec  = ( this->m_MatrixQ * qvec ) * this->m_MatrixP;
    vec = vec / vec.two_norm();
    VectorType vec2  = ( this->m_MatrixQ * iqvec ) * this->m_MatrixP;
    vec2 = vec2 / vec2.two_norm();
    VectorType qvec2 = ( this->m_MatrixP * vec2 ) * this->m_MatrixQ;
    qvec2 = qvec2 / qvec2.two_norm();
    if ( vnl_math_abs(  this->PearsonCorr(  this->m_MatrixP * vec2,  this->m_MatrixQ * qvec2 )  ) >
	 vnl_math_abs(  this->PearsonCorr(  this->m_MatrixP * vec,  this->m_MatrixQ * qvec )  ) )
      {
      vec = vec2;
      qvec = qvec2;
      }
    for( unsigned int j = 0; j < kk; j++ )
      {
      VectorType qj = this->m_VariatesP.get_column(j);
      vec = this->Orthogonalize( vec, qj );
      qj = this->m_VariatesQ.get_column(j);
      qvec = this->Orthogonalize( qvec, qj );
      }
    //    vec = this->SpatiallySmoothVector( vec, this->m_MaskImageP );
    //    qvec = this->SpatiallySmoothVector( qvec, this->m_MaskImageQ );
    qvec = qvec / qvec.two_norm();
    vec = vec / vec.two_norm();
    if ( this->m_UseLongitudinalFormulation > 1.e-9 )
      {
      vec = ( vec + qvec ) * 0.5; 
      qvec = vec;
      }
    this->SparsifyP( vec );    this->SparsifyQ( qvec );
    this->m_VariatesP.set_column( kk, vec );
    this->m_VariatesQ.set_column( kk, qvec );
    this->NormalizeWeightsByCovariance( kk, 1, 1 );
    totalcorr += vnl_math_abs( this->PearsonCorr(  this->m_MatrixP * vec,  this->m_MatrixQ * qvec ) );
    }
  return totalcorr;
}


template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::InitializeSCCA( unsigned int n_vecs, unsigned int seeder )
{
  RealType totalcorr = 0;

  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    unsigned int theseed = ( kk + 1 ) * seeder;
    VectorType   vec = this->InitializeV( this->m_MatrixP, theseed  );
    vec = vec / vec.two_norm();
    VectorType qvec = ( this->m_MatrixP * vec ) * this->m_MatrixQ;
    qvec = qvec / qvec.two_norm();
    vec = ( this->m_MatrixQ * qvec ) * this->m_MatrixP;
    vec = vec / vec.two_norm();
    for( unsigned int j = 0; j < kk; j++ )
      {
      VectorType qj = this->m_VariatesP.get_column(j);
      vec = this->Orthogonalize( vec, qj );
      qj = this->m_VariatesQ.get_column(j);
      qvec = this->Orthogonalize( qvec, qj );
      }
    //    vec = this->SpatiallySmoothVector( vec, this->m_MaskImageP );
    //    qvec = this->SpatiallySmoothVector( qvec, this->m_MaskImageQ );
    qvec = qvec / qvec.two_norm();
    vec = vec / vec.two_norm();
    this->SparsifyP( vec );    this->SparsifyQ( qvec );
    this->m_VariatesP.set_column( kk, vec );
    this->m_VariatesQ.set_column( kk, qvec );
    this->NormalizeWeights( kk );
    //    this->NormalizeWeightsByCovariance( kk, 1, 1 );
    totalcorr += vnl_math_abs( this->PearsonCorr(  this->m_MatrixP * vec,  this->m_MatrixQ * qvec ) );
    }
  return totalcorr;
  //  this->CCAUpdate( n_vecs , false );
  return this->m_CanonicalCorrelations.sum();
}




template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::SparsePartialArnoldiCCA(unsigned int n_vecs_in)
{
  this->m_Debug = false;
  unsigned int n_vecs = n_vecs_in;
  if( n_vecs < 1 )
    {
    n_vecs = 1;
    }
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  if ( ! m_Silent )
    {
    ::ants::antscout << " arnoldi sparse partial cca : L1?" << this->m_UseL1 << " GradStep " << this->m_GradStep
                     <<  "  p+ " << this->GetKeepPositiveP() << " q+ " << this->GetKeepPositiveQ() << std::endl;
    }
  this->m_MatrixP =  this->NormalizeMatrix( this->m_OriginalMatrixP, false );
  this->m_MatrixQ =  this->NormalizeMatrix( this->m_OriginalMatrixQ, false );
  this->m_MatrixR =  this->NormalizeMatrix( this->m_OriginalMatrixR, false );

  if( this->m_OriginalMatrixR.size() > 0 )
    {
    ::ants::antscout << "Partialing-Pre : -P-Norm  " << this->m_MatrixP.frobenius_norm() << " -Q-Norm  "
                     << this->m_MatrixQ.frobenius_norm() << std::endl;
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    if( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )
      {
      ::ants::antscout << " Subtract R from P " << std::endl;

      this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
      ::ants::antscout << "Partialing-Post : -P-Norm  " << this->m_MatrixP.frobenius_norm() <<  std::endl;
      }
    if( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )
      {
      ::ants::antscout << " Subtract R from Q " << std::endl;
      this->m_MatrixQ = this->m_MatrixQ - this->m_MatrixRRt * this->m_MatrixQ;
      ::ants::antscout << " -Q-Norm  " << this->m_MatrixQ.frobenius_norm() << std::endl;
      }
    }

  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_VariatesQ.set_size(this->m_MatrixQ.cols(), n_vecs);
  RealType initReturn = this->InitializeSCCA_simple( n_vecs );
  if ( !m_Silent )
    {
    ::ants::antscout << "Initialization: " << initReturn << std::endl;
    }
  /*
  RealType     bestcorr = this->InitializeSCCA_simple( n_vecs );
  RealType     totalcorr = 0;
  int bestseed = -1;
  for( unsigned int seeder = 0; seeder < 35; seeder++ )
    {
    totalcorr = this->InitializeSCCA( n_vecs, seeder );
    if( totalcorr > bestcorr )
      {
      bestseed = seeder;  bestcorr = totalcorr;
      ::ants::antscout << " seed " << seeder << " corr " << bestcorr << std::endl;
      }
    }
  if( this->m_Debug )
    {
    ::ants::antscout << " Best initial corr " << bestcorr << std::endl;
    }
  if ( bestseed >= 0 ) this->InitializeSCCA( n_vecs, bestseed ); */

  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
  unsigned int       loop = 0;
  bool               energyincreases = true;
  RealType           energy = 0;
  RealType           lastenergy = 0;
  while( ( ( loop < maxloop )  && ( energyincreases )  ) )
    {
    // Arnoldi Iteration SCCA
    bool normbycov = true;
    if ( !normbycov && loop == 0 ) this->m_GradStep *= 1.e-8;
    bool changedgrad = this->CCAUpdate( n_vecs_in, true , normbycov );
    lastenergy = energy;
    energy = this->m_CanonicalCorrelations.one_norm() / ( float ) n_vecs_in;
    // if( this->m_Debug )
    if ( ! m_Silent )
      {
      ::ants::antscout << " Loop " << loop << " Corrs : " << this->m_CanonicalCorrelations << " CorrMean : " << energy << std::endl;
      }
    if( this->m_GradStep < 1.e-12 || ( vnl_math_abs( energy - lastenergy ) < 1.e-8  && !changedgrad ) )
      {
      energyincreases = false;
      }
    else
      {
      energyincreases = true;
      }
    loop++;
    } // outer loop

  this->SortResults( n_vecs_in );
  //  this->RunDiagnostics(n_vecs);
  double ccasum = 0; for( unsigned int i = 0; i < this->m_CanonicalCorrelations.size(); i++ )
    {
    ccasum += fabs(this->m_CanonicalCorrelations[i]);
    }
  if( n_vecs_in > 1 )
    {
    return ccasum;
    }
  else
    {
    return fabs(this->m_CanonicalCorrelations[0]);
    }

/* **************************************************************************************************

// now deal with covariates --- this could work but needs to be fixed.
    for ( unsigned int j=0; j< this->m_MatrixR.cols(); j++) {
    // FIXME is this really what qj should be?  it should be the projection of the nuisance variable
    //    into the space of qj ...
      VectorType cov=this->m_MatrixR.get_column(j);
      VectorType qj=cov*this->m_MatrixP;
      RealType hjk=inner_product(cov,this->m_MatrixP*pveck)/
                   inner_product(cov,cov);
      if ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )
        for (unsigned int i=0; i<pveck.size(); i++)  pveck(i)=pveck(i)-hjk*qj(i);
      qj=cov*this->m_MatrixQ;
      hjk=inner_product(cov,this->m_MatrixQ*qveck)/
          inner_product(cov,cov);
      if ( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )
        for (unsigned int i=0; i<qveck.size(); i++)  qveck(i)=qveck(i)-hjk*qj(i);
    }

// evil code below ....

    if ( this->m_SCCANFormulation != PQ ) {
    for ( unsigned int dd=0; dd<20 ; dd++) {
      VectorType upp(this->m_MatrixP.cols(),0);
      VectorType upq(this->m_MatrixQ.cols(),0);
        for ( unsigned int rr=0; rr<this->m_MatrixR.cols(); rr++) {
          pveck=this->m_MatrixQ*qtemp;
          qveck=this->m_MatrixP*ptemp;
          pveck=pveck/pveck.two_norm();
          qveck=qveck/qveck.two_norm();
          pveck=this->Orthogonalize(pveck,this->m_OriginalMatrixR.get_column(rr));
          qveck=this->Orthogonalize(qveck,this->m_OriginalMatrixR.get_column(rr));
          VectorType tempp=this->m_MatrixP.transpose()*pveck;
          VectorType tempq=this->m_MatrixQ.transpose()*qveck;
          pveck=pveck/pveck.two_norm();
          upp=upp+tempp/tempp.two_norm()*1.0/(this->m_MatrixR.cols());
          qveck=qveck/qveck.two_norm();
          upq=upq+tempq/tempq.two_norm()*1.0/(this->m_MatrixR.cols());
        } //rr
      RealType eps=.01;
      if ( dd % 2 == 0 )
      {
        qtemp=qtemp+eps*upq;
//      RealType c1=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixQ*qtemp);
//      RealType c2=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixQ*(qtemp+eps*upq));
//      RealType c3=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixQ*(qtemp-eps*upq));
//      if ( fabs(c2) < fabs(c1) )  qtemp=qtemp+eps*upq;
//      if ( fabs(c3) < fabs(c1) )  qtemp=qtemp-eps*upq;
      }
      else
      {
        ptemp=ptemp+eps*upp;
//      RealType c1=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixP*ptemp);
//      RealType c2=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixP*(ptemp+eps*upp));
//      RealType c3=this->PearsonCorr(this->m_OriginalMatrixR.get_column(rr),this->m_MatrixP*(ptemp-eps*upp));
//      if ( fabs(c2) < fabs(c1) )  ptemp=ptemp+eps*upp;
//      if ( fabs(c3) < fabs(c1) )  ptemp=ptemp-eps*upp;
      }
      ptemp=ptemp/ptemp.two_norm();
      qtemp=qtemp/qtemp.two_norm();
      pveck=this->m_MatrixQ*qtemp;
      qveck=this->m_MatrixP*ptemp;
    } //dd
    } //      if ( this->m_SCCANFormulation != PQ ) {



       VectorType proj=this->m_MatrixQ*this->m_WeightsQ;
    if ( false && ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR ) )
        for (unsigned int kk=0; kk< this->m_OriginalMatrixR.cols(); kk++)
          proj=this->Orthogonalize(proj,this->m_MatrixR.get_column(kk));
        this->m_WeightsP=this->m_MatrixP.transpose()*(proj);
*/
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::IHTCCA(unsigned int n_vecs_in)
{
  ::ants::antscout << " IHTCCA " << std::endl;
  unsigned int n_vecs = n_vecs_in;

  if( n_vecs < 1 )
    {
    n_vecs = 1;
    }
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  ::ants::antscout << " arnoldi sparse partial cca " << std::endl;
  ::ants::antscout << "  pos-p " << this->GetKeepPositiveP() << " pos-q " << this->GetKeepPositiveQ() << std::endl;
  this->m_MatrixP = this->NormalizeMatrix( this->m_OriginalMatrixP, false );
  this->m_MatrixQ = this->NormalizeMatrix( this->m_OriginalMatrixQ, false );
  this->m_MatrixR = this->NormalizeMatrix( this->m_OriginalMatrixR, false );

  if( this->m_OriginalMatrixR.size() > 0 )
    {
    ::ants::antscout << "Partialing-Pre : -P-Norm  " << this->m_MatrixP.frobenius_norm() << " -Q-Norm  "
                     << this->m_MatrixQ.frobenius_norm() << std::endl;
    this->m_MatrixRRt = this->ProjectionMatrix(this->m_OriginalMatrixR);
    if( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )
      {
      ::ants::antscout << " Subtract R from P " << std::endl;
      this->m_MatrixP = this->m_MatrixP - (this->m_MatrixRRt * this->m_MatrixP);
      ::ants::antscout << "Partialing-Post : -P-Norm  " << this->m_MatrixP.frobenius_norm() <<  std::endl;
      }
    if( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )
      {
      ::ants::antscout << " Subtract R from Q " << std::endl;
      this->m_MatrixQ = this->m_MatrixQ - this->m_MatrixRRt * this->m_MatrixQ;
      ::ants::antscout << " -Q-Norm  " << this->m_MatrixQ.frobenius_norm() << std::endl;
      }
    }

  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_VariatesQ.set_size(this->m_MatrixQ.cols(), n_vecs);
  RealType     totalcorr = 0;
  RealType     bestcorr = 0;
  unsigned int bestseed = 1;
  for( unsigned int seeder = 1; seeder < 100; seeder++ )
    {
    totalcorr = this->InitializeSCCA( n_vecs, seeder );
    if( totalcorr > bestcorr )
      {
      bestseed = seeder;  bestcorr = totalcorr;
      }
    totalcorr = 0;
    }
  ::ants::antscout << " Best initial corr " << bestcorr << std::endl;
  this->InitializeSCCA( n_vecs, bestseed );
  const unsigned int maxloop = this->m_MaximumNumberOfIterations;
  unsigned int       loop = 0;
  bool               energyincreases = true;
  RealType           energy = 0, lastenergy = 0;
  while( ( ( loop < maxloop ) && ( energyincreases ) ) || loop < 1 ) //
    {
    // Arnoldi Iteration SCCA
    for( unsigned int k = 0; k < n_vecs; k++ )
      {
      VectorType ptemp = this->m_VariatesP.get_column(k);
      VectorType qtemp = this->m_VariatesQ.get_column(k);
      VectorType pveck = this->m_MatrixQ * qtemp;
      VectorType qveck = this->m_MatrixP * ptemp;
      pveck = this->m_MatrixP.transpose() * pveck;
      qveck = this->m_MatrixQ.transpose() * qveck;
      for( unsigned int j = 0; j < k; j++ )
        {
        VectorType qj = this->m_VariatesP.get_column(j);
        pveck = this->Orthogonalize( pveck, qj );
        qj = this->m_VariatesQ.get_column(j);
        qveck = this->Orthogonalize( qveck, qj );
        }
      RealType mup = inner_product( this->m_MatrixP * pveck, this->m_MatrixP * pveck ) / pveck.two_norm();
      RealType muq = inner_product( this->m_MatrixQ * qveck, this->m_MatrixQ * qveck ) / qveck.two_norm();
      this->IHTRegression(  this->m_MatrixP,  ptemp, pveck, mup, 5, 0, true, false );
      this->IHTRegression(  this->m_MatrixQ,  qtemp, qveck, muq, 5, 0, false, false );
      this->m_VariatesP.set_column( k, ptemp );
      this->m_VariatesQ.set_column( k, qtemp );
      this->NormalizeWeightsByCovariance( k, 0.05, 0.05 );
      VectorType proj1 =  this->m_MatrixP * this->m_VariatesP.get_column( k );
      VectorType proj2 =  this->m_MatrixQ * this->m_VariatesQ.get_column( k );
      this->m_CanonicalCorrelations[k] = this->PearsonCorr( proj1, proj2  );
      }
    this->SortResults( n_vecs );
    lastenergy = energy;
    energy = this->m_CanonicalCorrelations.one_norm() / n_vecs;
    ::ants::antscout << " Loop " << loop << " Corrs : " << this->m_CanonicalCorrelations << " CorrMean : " << energy
                     << std::endl;
    if( vnl_math_abs( energy - lastenergy ) < 1.e-8 || energy < lastenergy )
      {
      energyincreases = false;
      }
    else
      {
      energyincreases = true;
      }
    loop++;
    } // outer loop

  this->SortResults(n_vecs);
  //  this->RunDiagnostics(n_vecs);
  double ccasum = 0; for( unsigned int i = 0; i < this->m_CanonicalCorrelations.size(); i++ )
    {
    ccasum += fabs(this->m_CanonicalCorrelations[i]);
    }
  if( n_vecs_in > 1 )
    {
    return ccasum;
    }
  else
    {
    return fabs(this->m_CanonicalCorrelations[0]);
    }
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::WhitenDataSetForRunSCCANMultiple(unsigned int nvecs)
{
  if( this->m_Debug )
    {
    ::ants::antscout << " now whiten and apply R " << std::endl;
    }
  if( this->m_OriginalMatrixR.size() > 0 || nvecs > 0  )
    {
    this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
    if( this->m_VariatesP.size() > 0 )
      {
      this->m_MatrixRp.set_size(this->m_MatrixP.rows(), this->m_OriginalMatrixR.cols() + nvecs);
      this->m_MatrixRp.fill(0);
      if( this->m_OriginalMatrixR.size() > 0  &&
          ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR ) )
        {
        this->m_MatrixRp.set_columns(0, this->m_OriginalMatrixR);
        this->m_MatrixRp.set_columns(this->m_OriginalMatrixR.cols(),
                                     this->m_MatrixP * (this->m_VariatesP.get_n_columns(0, nvecs) ) );
        }
      else
        {
        this->m_MatrixRp.set_columns(0,
                                     this->m_MatrixP * (this->m_VariatesP.get_n_columns(0, nvecs) ) );
        }
      }
    else
      {
      this->m_MatrixRp = this->NormalizeMatrix(this->m_OriginalMatrixR);
      }
    this->m_MatrixRp = ProjectionMatrix(this->m_MatrixRp);
    if( this->m_Debug && this->m_VariatesP.cols() > 1 )
      {
      ::ants::antscout << " corr-pre "
                       << this->PearsonCorr( (this->m_OriginalMatrixP * this->m_VariatesP).get_column(0),
                            (this->m_OriginalMatrixP
                             * this->m_VariatesP).get_column(1) ) << std::endl; ::ants::antscout << " corr-post "
                                                                                                 << this->PearsonCorr( (
                              this->
                              m_MatrixP
                              * this->m_VariatesP).get_column(0),
                            (this->m_MatrixP * this->m_VariatesP).get_column(1) ) << std::endl;
      }

    this->m_MatrixQ = this->NormalizeMatrix(this->m_OriginalMatrixQ);
    if( this->m_VariatesQ.size() > 0 )
      {
      this->m_MatrixRq.set_size(this->m_MatrixQ.rows(), this->m_OriginalMatrixR.cols() + nvecs);
      this->m_MatrixRq.fill(0);
      if( this->m_OriginalMatrixR.size() > 0  &&
          ( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )  )
        {
        this->m_MatrixRq.set_columns(0, this->m_OriginalMatrixR);
        this->m_MatrixRq.set_columns(this->m_OriginalMatrixR.cols(),
                                     this->m_MatrixQ * (this->m_VariatesQ.get_n_columns(0, nvecs) ) );
        }
      else
        {
        this->m_MatrixRq.set_columns(0,
                                     this->m_MatrixQ * (this->m_VariatesQ.get_n_columns(0, nvecs) ) );
        }
      }
    else
      {
      this->m_MatrixRq = this->NormalizeMatrix(this->m_OriginalMatrixR);
      }
    this->m_MatrixRq = ProjectionMatrix(this->m_MatrixRq);
    }
  else
    {
    this->m_MatrixP = this->NormalizeMatrix(this->m_OriginalMatrixP);
    this->m_MatrixQ = this->NormalizeMatrix(this->m_OriginalMatrixQ);
    //     this->m_MatrixP=this->WhitenMatrix(this->m_MatrixP);
    //     this->m_MatrixQ=this->WhitenMatrix(this->m_MatrixQ);
    }

  if( this->m_Debug )
    {
    ::ants::antscout << "  whiten and apply R done " << std::endl;
    }
}


template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::NormalizeWeights(const unsigned int k )
{
  this->m_WeightsP = this->m_VariatesP.get_column( k );
  this->m_WeightsP = this->m_WeightsP / sqrt(this->m_WeightsP.two_norm()); // to scale gradient down
  this->m_VariatesP.set_column( k, this->m_WeightsP );
  this->m_WeightsQ = this->m_VariatesQ.get_column( k );
  this->m_WeightsQ = this->m_WeightsQ / sqrt(this->m_WeightsQ.two_norm()); // to scale gradient down
  this->m_VariatesQ.set_column( k, this->m_WeightsQ );
}

template <class TInputImage, class TRealType>
void antsSCCANObject<TInputImage, TRealType>
::NormalizeWeightsByCovariance(const unsigned int k, const TRealType taup, const TRealType tauq)
{
//  for ( unsigned int k=0; k<this->m_VariatesP.cols(); k++)
    {
    this->m_WeightsP = this->m_VariatesP.get_column( k );
    this->m_WeightsQ = this->m_VariatesQ.get_column( k );
    RealType normP = 0;
    if( this->m_MatrixRp.size() > 0 )
      {
      VectorType w = this->m_MatrixP * this->m_WeightsP;
      normP = inner_product( w, (this->m_MatrixP - this->m_MatrixRp * this->m_MatrixP) * this->m_WeightsP );
      }
    else
      {
      //  v^t ( X^t X + k * Id ) v = v^t  ( X X^t v + k * Id * v )
      //                           = v^t  ( X X^t v + k * Id * v )
      vnl_diag_matrix<double> regdiagp( this->m_MatrixP.cols(), taup );
      VectorType              w = this->m_MatrixP.transpose()
        * ( this->m_MatrixP * this->m_WeightsP ) + regdiagp * this->m_WeightsP;
      normP = inner_product( this->m_WeightsP, w );
      }
    if( normP > 0 )
      {
      if( this->m_Debug ) ::ants::antscout << "normP " << normP ;
      this->m_WeightsP = this->m_WeightsP / sqrt(normP);
      this->m_VariatesP.set_column( k, this->m_WeightsP );
      }
    RealType normQ = 0;
    if( this->m_MatrixRq.size() > 0 )
      {
      VectorType w = this->m_MatrixQ * this->m_WeightsQ;
      normQ = inner_product( w, (this->m_MatrixQ - this->m_MatrixRq * this->m_MatrixQ) * this->m_WeightsQ );
      }
    else
      {
      vnl_diag_matrix<double> regdiagq( this->m_MatrixQ.cols(), tauq );
      VectorType              w = this->m_MatrixQ.transpose()
        * ( this->m_MatrixQ * this->m_WeightsQ ) + regdiagq * this->m_WeightsQ;
      normQ = inner_product( this->m_WeightsQ, w );
      }
    if( normQ > 0 )
      {
      if( this->m_Debug ) ::ants::antscout << " normQ " << normQ << " ";
      this->m_WeightsQ = this->m_WeightsQ / sqrt(normQ);
      this->m_VariatesQ.set_column( k, this->m_WeightsQ );
      }
    }
}

template <class TInputImage, class TRealType>
TRealType
antsSCCANObject<TInputImage, TRealType>
::RunSCCAN2multiple( unsigned int n_vecs )
{
  this->m_Debug = false;
//  this->m_Debug=true;
  ::ants::antscout << " power iteration (partial) scca " << std::endl;
  this->m_CanonicalCorrelations.set_size(n_vecs);
  this->m_CanonicalCorrelations.fill(0);
  RealType     truecorr = 0;
  unsigned int nr1 = this->m_MatrixP.rows();
  unsigned int nr2 = this->m_MatrixQ.rows();
  this->m_VariatesP.set_size(0, 0);
  this->m_VariatesQ.set_size(0, 0);
  if( nr1 != nr2 )
    {
    ::ants::antscout << " P rows " << this->m_MatrixP.rows() << " cols " << this->m_MatrixP.cols() << std::endl;
    ::ants::antscout << " Q rows " << this->m_MatrixQ.rows() << " cols " << this->m_MatrixQ.cols() << std::endl;
    ::ants::antscout << " R rows " << this->m_MatrixR.rows() << " cols " << this->m_MatrixR.cols() << std::endl;
    ::ants::antscout << " N-rows for MatrixP does not equal N-rows for MatrixQ " << nr1 << " vs " << nr2 << std::endl;
    std::exception();
    }
  if(  !this->m_AlreadyWhitened  )
    {
    if( this->m_Debug )
      {
      ::ants::antscout << " whiten " << std::endl;
      }
    this->WhitenDataSetForRunSCCANMultiple();
    this->m_AlreadyWhitened = true;
    if( this->m_Debug )
      {
      ::ants::antscout << " whiten done " << std::endl;
      }
    }
  this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  this->m_VariatesQ.set_size(this->m_MatrixQ.cols(), n_vecs);
  for( unsigned int kk = 0; kk < n_vecs; kk++ )
    {
    this->m_VariatesP.set_column(kk, this->InitializeV(this->m_MatrixP) );
    this->m_VariatesQ.set_column(kk, this->InitializeV(this->m_MatrixQ) );
    }
// begin computing solution
  for( unsigned int makesparse = 1; makesparse < 2; makesparse++ )
    {
    unsigned int which_e_vec = 0;
    bool         notdone = true;
    while( notdone )
      {
      if( this->m_Debug )
        {
        ::ants::antscout << " get canonical variate number " << which_e_vec + 1 << std::endl;
        }
      double initcorr = 1.e-5;
      truecorr = initcorr;
      double deltacorr = 1, lastcorr = initcorr * 0.5;
      this->m_WeightsP = this->m_VariatesP.get_column(which_e_vec);
      this->m_WeightsQ = this->m_VariatesQ.get_column(which_e_vec);
      unsigned long its = 0, min_its = 5;
      if( this->m_Debug )
        {
        ::ants::antscout << " Begin " << std::endl;
        }
      while( (its<this->m_MaximumNumberOfIterations && deltacorr> this->m_ConvergenceThreshold)
             || its < min_its )
        {
        if( its == 0 )
          {
          this->WhitenDataSetForRunSCCANMultiple(which_e_vec);
          }
        bool doorth = true; // this->m_Debug=true;
          {
          VectorType proj = this->m_MatrixQ * this->m_WeightsQ;
          if( this->m_MatrixRp.size() > 0 &&
              ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR )  )
            {
            this->m_WeightsP = this->m_MatrixP.transpose() * (proj - this->m_MatrixRp * proj);
            }
          else
            {
            this->m_WeightsP = this->m_MatrixP.transpose() * (proj);
            }
          if( doorth )
            {
            for( unsigned int kk = 0; kk < which_e_vec; kk++ )
              {
              this->m_WeightsP = this->Orthogonalize(this->m_WeightsP, this->m_VariatesP.get_column(
                                                       kk), &this->m_MatrixP, &this->m_MatrixP);
              }
            }
          this->m_WeightsP = this->SoftThreshold( this->m_WeightsP, this->m_FractionNonZeroP, !this->m_KeepPositiveP );
          if( its > 0 )
            {
            this->m_WeightsP = this->ClusterThresholdVariate( this->m_WeightsP, this->m_MaskImageP );
            }
          if( which_e_vec > 0   && this->m_Debug   )
            {
            ::ants::antscout << " p orth-b "
                             << this->PearsonCorr( this->m_MatrixP * this->m_WeightsP, this->m_MatrixP
                                  * this->m_VariatesP.get_column(
                                    0) ) << std::endl;
            }
          }
        VectorType projp = this->m_MatrixQ * this->m_WeightsQ;
        VectorType projq = this->m_MatrixP * this->m_WeightsP;

          {
          VectorType proj = this->m_MatrixP * this->m_WeightsP;
          if( this->m_MatrixRq.size() > 0  &&
              ( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR )   )
            {
            this->m_WeightsQ = this->m_MatrixQ.transpose() * (proj - this->m_MatrixRq * proj);
            }
          else
            {
            this->m_WeightsQ = this->m_MatrixQ.transpose() * (proj);
            }
          if( doorth )
            {
            for( unsigned int kk = 0; kk < which_e_vec; kk++ )
              {
              this->m_WeightsQ = this->Orthogonalize(this->m_WeightsQ, this->m_VariatesQ.get_column(
                                                       kk), &this->m_MatrixQ, &this->m_MatrixQ);
              }
            }
          this->m_WeightsQ = this->SoftThreshold( this->m_WeightsQ, this->m_FractionNonZeroQ, !this->m_KeepPositiveQ );
          if( which_e_vec > 0 && this->m_Debug )
            {
            ::ants::antscout << " q orth-b "
                             << this->PearsonCorr( this->m_MatrixQ * this->m_WeightsQ, this->m_MatrixQ
                                  * this->m_VariatesQ.get_column(
                                    0) ) << std::endl;
            }
          }
        this->m_WeightsP = this->m_MatrixP.transpose() * (projp);
        this->m_WeightsQ = this->m_MatrixQ.transpose() * (projq);
        this->ReSoftThreshold( this->m_WeightsP, this->m_FractionNonZeroP, this->m_KeepPositiveP );
        this->ReSoftThreshold( this->m_WeightsQ, this->m_FractionNonZeroQ, this->m_KeepPositiveQ );
        if( its > 1 )
          {
          this->m_WeightsP = this->ClusterThresholdVariate( this->m_WeightsP, this->m_MaskImageP,
                                                            this->m_MinClusterSizeP );
          this->m_WeightsQ = this->ClusterThresholdVariate( this->m_WeightsQ, this->m_MaskImageQ,
                                                            this->m_MinClusterSizeQ );
          }
        for( unsigned int kk = 0; kk < which_e_vec; kk++ )
          {
          this->m_WeightsP = this->Orthogonalize(this->m_WeightsP, this->m_VariatesP.get_column(
                                                   kk), &this->m_MatrixP, &this->m_MatrixP);
          }
        if( ( this->m_SCCANFormulation == PminusRQ ||  this->m_SCCANFormulation == PminusRQminusR ) )
          {
          for( unsigned int kk = 0; kk < this->m_OriginalMatrixR.cols(); kk++ )
            {
            this->m_WeightsP = this->Orthogonalize(this->m_WeightsP, this->m_MatrixR.get_column(
                                                     kk) * this->m_MatrixP, &this->m_MatrixP, &this->m_MatrixP);
            }
          }
        for( unsigned int kk = 0; kk < which_e_vec; kk++ )
          {
          this->m_WeightsQ = this->Orthogonalize(this->m_WeightsQ, this->m_VariatesQ.get_column(
                                                   kk), &this->m_MatrixQ, &this->m_MatrixQ);
          }
        if( ( this->m_SCCANFormulation == PQminusR ||  this->m_SCCANFormulation == PminusRQminusR ) )
          {
          for( unsigned int kk = 0; kk < this->m_OriginalMatrixR.cols(); kk++ )
            {
            this->m_WeightsQ = this->Orthogonalize(this->m_WeightsQ, this->m_MatrixR.get_column(
                                                     kk) * this->m_MatrixQ, &this->m_MatrixQ, &this->m_MatrixQ);
            }
          }

        this->NormalizeWeightsByCovariance(which_e_vec);
        this->m_VariatesP.set_column(which_e_vec, this->m_WeightsP);
        this->m_VariatesQ.set_column(which_e_vec, this->m_WeightsQ);
        truecorr = this->PearsonCorr( this->m_MatrixP * this->m_WeightsP, this->m_MatrixQ * this->m_WeightsQ );
        if( this->m_Debug )
          {
          ::ants::antscout << " corr " << truecorr << " it " << its << std::endl;
          }
        deltacorr = fabs(truecorr - lastcorr);
        lastcorr = truecorr;
        ++its;
        this->m_CanonicalCorrelations[which_e_vec] = truecorr;
        ::ants::antscout << "  canonical variate number " << which_e_vec + 1 << " corr "
                         << this->m_CanonicalCorrelations[which_e_vec]  << " kept cluster " << this->m_KeptClusterSize
                         << std::endl;
        } // inner_it

      if( fabs(truecorr) < 1.e-2 || (which_e_vec + 1) == n_vecs )
        {
        notdone = false;
        }
      else
        {
        which_e_vec++;
        }
      }

    if( this->m_Debug )
      {
      ::ants::antscout << " done with loop " << std::endl;
      }
    std::vector<TRealType> evals(n_vecs, 0);
    std::vector<TRealType> oevals(n_vecs, 0);
    for( unsigned long j = 0; j < n_vecs; ++j )
      {
      RealType val = fabs(this->m_CanonicalCorrelations[j]);
      evals[j] = val;
      oevals[j] = val;
      }

// sort and reindex the eigenvectors/values
    sort(evals.begin(), evals.end(), my_sccan_sort_object);
    std::vector<int> sorted_indices(n_vecs, -1);
    for( unsigned int i = 0; i < evals.size(); i++ )
      {
      for( unsigned int j = 0; j < evals.size(); j++ )
        {
        if( evals[i] == oevals[j] &&  sorted_indices[i] == -1 )
          {
          sorted_indices[i] = j;
          oevals[j] = 0;
          }
        }
      }

    VectorType newcorrs(n_vecs, 0);
    MatrixType varp(this->m_MatrixP.cols(), n_vecs, 0);
    MatrixType varq(this->m_MatrixQ.cols(), n_vecs, 0);
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      varp.set_column(i, this->m_VariatesP.get_column( sorted_indices[i] ) );
      varq.set_column(i, this->m_VariatesQ.get_column( sorted_indices[i] ) );
      newcorrs[i] = (this->m_CanonicalCorrelations[sorted_indices[i]]);
      }
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      this->m_VariatesP.set_column(i, varp.get_column( i ) );
      this->m_VariatesQ.set_column(i, varq.get_column( i ) );
      }
    this->m_CanonicalCorrelations = newcorrs;
//  this->RunDiagnostics(n_vecs);
    } // makesparse

  RealType corrsum = 0;
  for( unsigned int i = 0; i < this->m_CanonicalCorrelations.size(); i++ )
    {
    corrsum += fabs(this->m_CanonicalCorrelations[i]);
    }
  return this->m_CanonicalCorrelations[0]; // corrsum;
}

template <class TInputImage, class TRealType>
TRealType antsSCCANObject<TInputImage, TRealType>
::RunSCCAN2()
{
  RealType     truecorr = 0;
  unsigned int nr1 = this->m_MatrixP.rows();
  unsigned int nr2 = this->m_MatrixQ.rows();

  if( nr1 != nr2 )
    {
    ::ants::antscout << " P rows " << this->m_MatrixP.rows() << " cols " << this->m_MatrixP.cols() << std::endl;
    ::ants::antscout << " Q rows " << this->m_MatrixQ.rows() << " cols " << this->m_MatrixQ.cols() << std::endl;
    ::ants::antscout << " R rows " << this->m_MatrixR.rows() << " cols " << this->m_MatrixR.cols() << std::endl;
    ::ants::antscout << " N-rows for MatrixP does not equal N-rows for MatrixQ " << nr1 << " vs " << nr2 << std::endl;
    std::exception();
    }
  else
    {
//  ::ants::antscout << " P-positivity constraints? " <<  this->m_KeepPositiveP << " frac " << this->m_FractionNonZeroP
// << "
// Q-positivity constraints?  " << m_KeepPositiveQ << " frac " << this->m_FractionNonZeroQ << std::endl;
    }
  this->m_WeightsP = this->InitializeV(this->m_MatrixP);
  this->m_WeightsQ = this->InitializeV(this->m_MatrixQ);

//  if ( !this->m_AlreadyWhitened )
    {
    if( this->m_Debug )
      {
      ::ants::antscout << " norm P " << std::endl;
      }
    this->m_MatrixP = this->NormalizeMatrix(this->m_MatrixP);
    if( this->m_Debug )
      {
      ::ants::antscout << " norm Q " << std::endl;
      }
    this->m_MatrixQ = this->NormalizeMatrix(this->m_MatrixQ);
    if( this->m_OriginalMatrixR.size() > 0 )
      {
      this->m_MatrixR = this->NormalizeMatrix(this->m_OriginalMatrixR);
      this->m_MatrixR = this->WhitenMatrix(this->m_MatrixR);
      this->m_MatrixRRt = this->m_MatrixR * this->m_MatrixR.transpose();
      this->UpdatePandQbyR();
      }
    this->m_MatrixP = this->WhitenMatrix(this->m_MatrixP);
    this->m_MatrixQ = this->WhitenMatrix(this->m_MatrixQ);
    this->m_AlreadyWhitened = true;
    }
  for( unsigned int outer_it = 0; outer_it < 2; outer_it++ )
    {
    truecorr = 0;
    double        deltacorr = 1, lastcorr = 1;
    unsigned long its = 0;
    while( its<this->m_MaximumNumberOfIterations && deltacorr> this->m_ConvergenceThreshold  )
      {
      this->m_WeightsP =
        this->TrueCCAPowerUpdate(this->m_FractionNonZeroP, this->m_MatrixP, this->m_WeightsQ, this->m_MatrixQ,
                                 this->m_KeepPositiveP,
                                 false);
      this->m_WeightsQ =
        this->TrueCCAPowerUpdate(this->m_FractionNonZeroQ, this->m_MatrixQ, this->m_WeightsP, this->m_MatrixP,
                                 this->m_KeepPositiveQ,
                                 false);
      truecorr = this->PearsonCorr( this->m_MatrixP * this->m_WeightsP, this->m_MatrixQ * this->m_WeightsQ );
      deltacorr = fabs(truecorr - lastcorr);
      lastcorr = truecorr;
      ++its;
      } // inner_it
    }   // outer_it
  this->m_CorrelationForSignificanceTest = truecorr;
  return truecorr;
}

template <class TInputImage, class TRealType>
TRealType
antsSCCANObject<TInputImage, TRealType>
::RunSCCAN3()
{
  unsigned int nc1 = this->m_MatrixP.rows();
  unsigned int nc2 = this->m_MatrixQ.rows();
  unsigned int nc3 = this->m_MatrixR.rows();

  if( nc1 != nc2 || nc1 != nc3 || nc3 != nc2 )
    {
    ::ants::antscout << " P rows " << this->m_MatrixP.rows() << " cols " << this->m_MatrixP.cols() << std::endl;
    ::ants::antscout << " Q rows " << this->m_MatrixQ.rows() << " cols " << this->m_MatrixQ.cols() << std::endl;
    ::ants::antscout << " R rows " << this->m_MatrixR.rows() << " cols " << this->m_MatrixR.cols() << std::endl;
    ::ants::antscout << " N-rows do not match "  << std::endl;
    std::exception();
    }

  this->m_WeightsP = this->InitializeV(this->m_MatrixP);
  this->m_WeightsQ = this->InitializeV(this->m_MatrixQ);
  this->m_WeightsR = this->InitializeV(this->m_MatrixR);
  if( !this->m_AlreadyWhitened )
    {
    this->m_MatrixP = this->NormalizeMatrix(this->m_MatrixP);
    this->m_MatrixP = this->WhitenMatrix(this->m_MatrixP);
    this->m_MatrixQ = this->NormalizeMatrix(this->m_MatrixQ);
    this->m_MatrixQ = this->WhitenMatrix(this->m_MatrixQ);
    this->m_MatrixR = this->NormalizeMatrix(this->m_MatrixR);
    this->m_MatrixR = this->WhitenMatrix(this->m_MatrixR);
    this->m_AlreadyWhitened = true;
    }
  RealType      truecorr = 0;
  RealType      norm = 0, deltacorr = 1, lastcorr = 1;
  unsigned long its = 0;
  while( its<this->m_MaximumNumberOfIterations && deltacorr> this->m_ConvergenceThreshold  )
    {
    /** for sparse mcca
     *     w_i \leftarrow \frac{ S( X_i^T ( \sum_{j \ne i} X_j w_j  ) }{norm of above }
     */
    this->m_WeightsP = this->m_MatrixP.transpose()
      * (this->m_MatrixQ * this->m_WeightsQ + this->m_MatrixR * this->m_WeightsR);
    this->ReSoftThreshold( this->m_WeightsP, this->m_FractionNonZeroP, this->m_KeepPositiveP);
    norm = this->m_WeightsP.two_norm();
    this->m_WeightsP = this->m_WeightsP / (norm);

    this->m_WeightsQ = this->m_MatrixQ.transpose()
      * (this->m_MatrixP * this->m_WeightsP + this->m_MatrixR * this->m_WeightsR);
    this->ReSoftThreshold( this->m_WeightsQ, this->m_FractionNonZeroQ, this->m_KeepPositiveQ);
    norm = this->m_WeightsQ.two_norm();
    this->m_WeightsQ = this->m_WeightsQ / (norm);

    this->m_WeightsR = this->m_MatrixR.transpose()
      * (this->m_MatrixP * this->m_WeightsP + this->m_MatrixQ * this->m_WeightsQ);
    this->ReSoftThreshold( this->m_WeightsR, this->m_FractionNonZeroR, this->m_KeepPositiveR);
    norm = this->m_WeightsR.two_norm();
    this->m_WeightsR = this->m_WeightsR / (norm);

    VectorType pvec = this->m_MatrixP * this->m_WeightsP;
    VectorType qvec = this->m_MatrixQ * this->m_WeightsQ;
    VectorType rvec = this->m_MatrixR * this->m_WeightsR;

    double corrpq = this->PearsonCorr( pvec, qvec );
    double corrpr = this->PearsonCorr( pvec, rvec );
    double corrqr = this->PearsonCorr( rvec, qvec );
    truecorr = corrpq + corrpr + corrqr;
    deltacorr = fabs(truecorr - lastcorr);
    lastcorr = truecorr;
    // ::ants::antscout << " correlation of projections: pq " << corrpq << " pr " << corrpr << " qr " << corrqr << "
    // at-it " <<
    // its << std::endl;
    its++;
    }

  //  ::ants::antscout << " PNZ-Frac " << this->CountNonZero(this->m_WeightsP) << std::endl;
  //  ::ants::antscout << " QNZ-Frac " << this->CountNonZero(this->m_WeightsQ) << std::endl;
  //  ::ants::antscout << " RNZ-Frac " << this->CountNonZero(this->m_WeightsR) << std::endl;

  this->m_CorrelationForSignificanceTest = truecorr;
  return truecorr;
}

template <class TInputImage, class TRealType>
void
antsSCCANObject<TInputImage, TRealType>
::MRFFilterVariateMatrix()
{
  // 1. compute the label for each voxel --- the label is the col + 1
  // recall this->m_VariatesP.set_size(this->m_MatrixP.cols(), n_vecs);
  VectorType   labels( this->m_MatrixP.cols(), 0 );
  VectorType   maxweight(  this->m_MatrixP.cols(), 0 );
  unsigned int n_vecs = this->m_VariatesP.cols();

  for( unsigned int i = 0; i <  this->m_MatrixP.cols(); i++ )
    {
    RealType   maxval = 0;
    VectorType pvec = this->m_VariatesP.get_row( i );
    for( unsigned int j = 0; j < pvec.size(); j++ )
      {   /** FIXME should this be fabs or not? */
          //      RealType val = fabs( this->m_VariatesP( j , i ) );
      RealType val = pvec( j );
      if( val > maxval )
        {
        maxval = val;
        labels( i ) = ( j + 1 );
        maxweight( i ) = val;
        }
      }
    }
  typename TInputImage::Pointer flabelimage =
    this->ConvertVariateToSpatialImage( labels,  this->m_MaskImageP, false );
  typename TInputImage::Pointer maxweightimage =
    this->ConvertVariateToSpatialImage( maxweight,  this->m_MaskImageP, false );

  typedef unsigned int                          LabelType;
  typedef itk::Image<LabelType, ImageDimension> LabelImageType;
  typename LabelImageType::Pointer labelimage = LabelImageType::New();
  labelimage->SetRegions( flabelimage->GetRequestedRegion() );
  labelimage->CopyInformation( flabelimage );
  labelimage->Allocate();
  labelimage->FillBuffer( 0 );
  typename LabelImageType::Pointer maskimage = LabelImageType::New();
  maskimage->SetRegions( flabelimage->GetRequestedRegion() );
  maskimage->CopyInformation( flabelimage );
  maskimage->Allocate();
  maskimage->FillBuffer( 0 );
  itk::ImageRegionConstIterator<TInputImage>
  Itf( flabelimage, flabelimage->GetLargestPossibleRegion() );
  for( Itf.GoToBegin(); !Itf.IsAtEnd(); ++Itf )
    {
    if( this->m_MaskImageP->GetPixel( Itf.GetIndex() ) > 0 )
      {
      labelimage->SetPixel( Itf.GetIndex(), static_cast<LabelType>( Itf.Get() + 0.5 ) );
      maskimage->SetPixel( Itf.GetIndex(), 1 );
      }
    }
  // simple MRF style update
  for( unsigned int mrfct = 0; mrfct < 2; mrfct++ )
    {
    typedef itk::NeighborhoodIterator<LabelImageType> iteratorType;
    typename iteratorType::RadiusType rad;
    rad.Fill(1);
    iteratorType GHood(rad, labelimage, labelimage->GetLargestPossibleRegion() );
    GHood.GoToBegin();
    while( !GHood.IsAtEnd() )
      {
      VectorType countlabels( n_vecs, 0 );
      LabelType  p = GHood.GetCenterPixel();
      if( p >= 0.5 && this->m_MaskImageP->GetPixel( GHood.GetIndex() )  > 0.5 )
        {
        for( unsigned int i = 0; i < GHood.Size(); i++ )
          {
          LabelType p2 = GHood.GetPixel(i);
          if( p2 >  0 )
            {
            countlabels[p2 - 1] = countlabels[p2 - 1] + 1;
            }
          }
        LabelType     jj = 0;
        unsigned long maxlabcount = 0;
        for( unsigned int i = 0; i < countlabels.size(); i++ )
          {
          if( countlabels[i] > maxlabcount )
            {
            jj = i + 1;
            maxlabcount = countlabels[i];
            }
          }
        GHood.SetCenterPixel( jj );
        }
      ++GHood;
      }
    } // mrfct

  unsigned int vecind = 0;
  for(  Itf.GoToBegin(); !Itf.IsAtEnd(); ++Itf )
    {
    if( this->m_MaskImageP->GetPixel( Itf.GetIndex() ) > 0.5 )
      {
      labels( vecind ) = labelimage->GetPixel( Itf.GetIndex() );
      vecind++;
      }
    }

    {
    typedef  itk::ImageFileWriter<LabelImageType> WriterType;
    typename WriterType::Pointer writer = WriterType::New();
    writer->SetInput( labelimage );
    writer->SetFileName( "label1.nii.gz" );
    writer->Update();
    }
    {
    typedef  itk::ImageFileWriter<TInputImage> WriterType;
    typename WriterType::Pointer writer = WriterType::New();
    writer->SetInput( maxweightimage );
    writer->SetFileName( "weight.nii.gz" );
    writer->Update();
    }
  for( unsigned int i = 0; i <  this->m_MatrixP.cols(); i++ )
    {
    VectorType pvec = this->m_VariatesP.get_row( i );
    LabelType  lab = static_cast<LabelType>( labels( i ) + 0.5 );
    if( lab > 0 )
      {
      lab = lab - 1;
      }
    for( unsigned int j = 0; j < pvec.size(); j++ )
      {
      if( j != lab )
        {
        pvec( j ) = 0;
        }
      }
    pvec = pvec / pvec.two_norm();
    this->m_VariatesP.set_row( i, pvec );
    }

  /*

 // 2. now do a simple MRF style update
  typedef  itk::ants::AtroposSegmentationImageFilter
    <TInputImage, LabelImageType> SegmentationFilterType;
  typename SegmentationFilterType::Pointer segmenter
    = SegmentationFilterType::New();
  typename SegmentationFilterType::ArrayType radius;
  radius.Fill( 1 );
  segmenter->SetMinimizeMemoryUsage( false );
  segmenter->SetNumberOfTissueClasses( this->m_VariatesP.cols() );
  segmenter->SetInitializationStrategy(  SegmentationFilterType::PriorLabelImage );
  segmenter->SetPriorProbabilityWeight( 0.5 );
  segmenter->SetPriorLabelImage( labelimage );
  segmenter->SetPosteriorProbabilityFormulation(
     SegmentationFilterType::Socrates );
  segmenter->SetMaximumNumberOfIterations( 5 );
  segmenter->SetConvergenceThreshold( 0 );
  segmenter->SetMaskImage( maskimage );

      // Check to see that the labels in the prior label image or the non-zero
      // probability voxels in the prior probability images encompass the entire
      // mask region.

  if( segmenter->GetInitializationStrategy() ==
       SegmentationFilterType::PriorLabelImage )
    {
      itk::ImageRegionConstIterator<LabelImageType> ItM( segmenter->GetMaskImage(),
               segmenter->GetMaskImage()->GetLargestPossibleRegion() );
      itk::ImageRegionConstIterator<LabelImageType> ItP( segmenter->GetPriorLabelImage(),
               segmenter->GetPriorLabelImage()->GetLargestPossibleRegion() );
      for( ItM.GoToBegin(), ItP.GoToBegin(); !ItM.IsAtEnd(); ++ItM, ++ItP )
  {
          if( ItM.Get() == segmenter->GetMaskLabel() && ItP.Get() == 0 )
            {
      ::ants::antscout  << std::endl;
            ::ants::antscout  << "Warning: the labels in the the prior label image do "
                      << "not encompass the entire mask region.  As a result each unlabeled voxel will be "
                      << "initially assigned a random label.  The user might want to consider "
                      << "various alternative strategies like assigning an additional "
                      << "\"background\" label to the unlabeled voxels or propagating "
                      << "the labels within the mask region."
                      << std::endl;
            ::ants::antscout  << std::endl;
            break;
            }
  }
    }

  segmenter->SetIntensityImage( 0 , maxweightimage );
  segmenter->SetMRFSmoothingFactor( 0.2 );
  segmenter->SetMRFRadius( radius );
  typedef typename SegmentationFilterType::SampleType SampleType;
  typedef itk::ants::Statistics::GaussianListSampleFunction
    <SampleType, RealType, RealType> LikelihoodType;
  for( unsigned int n = 0; n < segmenter->GetNumberOfTissueClasses(); n++ )
    {
    typename LikelihoodType::Pointer gaussianLikelihood =
      LikelihoodType::New();
    segmenter->SetLikelihoodFunction( n, gaussianLikelihood );
    }
  segmenter->SetUsePartialVolumeLikelihoods( false );
  segmenter->Update();
  typename LabelImageType::Pointer labelimage2 = segmenter->GetOutput();
  {
    typedef  itk::ImageFileWriter<LabelImageType> WriterType;
    typename WriterType::Pointer writer = WriterType::New();
    writer->SetInput( labelimage2 );
    writer->SetFileName( "label2.nii.gz" );
    writer->Update();
  }
  */
}
} // namespace ants
} // namespace itk

/*

      if (its == 0 )
      if ( which_e_vec > 0   && false )
       {
// here, factor out previous evecs globally
    MatrixType temp;
    MatrixType pp;
    unsigned int basect=this->m_MatrixR.columns();
    basect=0;
    pp.set_size(this->m_MatrixP.rows(),which_e_vec+basect);
//    for (unsigned int kk=0; kk<this->m_MatrixR.columns(); kk++)
//          pp.set_column(kk,this->m_MatrixR.get_column(kk));
    unsigned int colcount=0; //this->m_MatrixR.columns();
    for (unsigned int kk=which_e_vec-1; kk<which_e_vec; kk++) {
          pp.set_column(colcount,this->m_MatrixP*this->m_VariatesP.get_column(kk));
      colcount++;
        }
        temp=this->NormalizeMatrix(pp);
        temp=this->WhitenMatrix(temp);
        temp=temp*temp.transpose();
        this->m_MatrixP=(this->m_MatrixP-temp*this->m_MatrixP);

    MatrixType qq;
    qq.set_size(this->m_MatrixQ.rows(),which_e_vec+this->m_MatrixR.columns());
    for (unsigned int kk=0; kk<this->m_MatrixR.columns(); kk++)
          qq.set_column(kk,this->m_MatrixR.get_column(kk));
    colcount=this->m_MatrixR.columns();
    for (unsigned int kk=which_e_vec-1; kk<which_e_vec; kk++) {
          qq.set_column(colcount,this->m_MatrixQ*this->m_VariatesQ.get_column(kk));
      colcount++;
        }
        temp=this->NormalizeMatrix(qq);
        temp=this->WhitenMatrix(temp);
        temp=temp*temp.transpose();
        this->m_MatrixQ=(this->m_MatrixQ-temp*this->m_MatrixQ);
      }




//m_Debug=true;
      double ip=1; unsigned long ct=0,max_ip_its=50;
      double deltaip=1,lastip=0;
      while ( (deltaip) > 1.e-3 && ct < max_ip_its && which_e_vec > 0 || ct < 4 ) {
        ip=0;
        this->ReSoftThreshold( this->m_WeightsP , this->m_FractionNonZeroP , this->m_KeepPositiveP );
        VectorType ptem=this->m_WeightsP;
    if ( which_e_vec >= 1 )
          ptem=this->Orthogonalize(ptem,this->m_VariatesP.get_column(0),&this->m_MatrixP);
      if ( which_e_vec >= 2 )
          ptem=this->Orthogonalize(ptem,this->m_VariatesP.get_column(1),&this->m_MatrixP);
    this->m_WeightsP=ptem;
        this->ReSoftThreshold( this->m_WeightsP , this->m_FractionNonZeroP , this->m_KeepPositiveP );
        ip+=this->PearsonCorr(this->m_MatrixP*this->m_WeightsP,this->m_MatrixP*this->m_VariatesP.get_column(0));
    if ( which_e_vec >= 2)
          ip+=this->PearsonCorr(this->m_MatrixP*this->m_WeightsP,this->m_MatrixP*this->m_VariatesP.get_column(1));
    deltaip=fabs(lastip)-fabs(ip);
    lastip=ip;
    ct++;
         if ( this->m_Debug ) ::ants::antscout << " pip-b " << ip << " delt " << deltaip << std::endl;
        }

       ip=1; ct=0;
       deltaip=1;lastip=0;
      while ( (deltaip) > 1.e-3 && ct < max_ip_its && which_e_vec > 0  || ct < 4 ) {
        ip=0;
        this->ReSoftThreshold( this->m_WeightsQ , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
        VectorType ptem=this->m_WeightsQ;
    if ( which_e_vec >= 1 )
          ptem=this->Orthogonalize(ptem,this->m_VariatesQ.get_column(0),&this->m_MatrixQ);
      if ( which_e_vec >= 2 )
          ptem=this->Orthogonalize(ptem,this->m_VariatesQ.get_column(1),&this->m_MatrixQ);
    this->m_WeightsQ=ptem;
        this->ReSoftThreshold( this->m_WeightsQ , this->m_FractionNonZeroQ , this->m_KeepPositiveQ );
        ip+=this->PearsonCorr(this->m_MatrixQ*this->m_WeightsQ,this->m_MatrixQ*this->m_VariatesQ.get_column(0));
    if ( which_e_vec >= 2)
          ip+=this->PearsonCorr(this->m_MatrixQ*this->m_WeightsQ,this->m_MatrixQ*this->m_VariatesQ.get_column(1));
    deltaip=fabs(lastip)-fabs(ip);
    lastip=ip;
    ct++;
         if ( this->m_Debug ) ::ants::antscout << " qip-b " << ip << " delt " << deltaip << std::endl;
        }


// alternative tools for factoring out evecs
//      if ( its == 0 && which_e_vec > 0  ) {
//        this->WhitenDataSetForRunSCCANMultiple();
        q_evecs_factor=this->m_MatrixQ*this->m_VariatesQ.get_n_columns(0,which_e_vec);
        q_evecs_factor=this->NormalizeMatrix( q_evecs_factor );
        q_evecs_factor=this->WhitenMatrix(q_evecs_factor);
        q_evecs_factor=q_evecs_factor*q_evecs_factor.transpose();
 //       MatrixType temp=this->m_MatrixP-q_evecs_factor*this->m_MatrixP;
 //       temp=this->InverseCovarianceMatrix(temp,&this->m_MatrixP);
 //       this->m_MatrixP=temp;

        p_evecs_factor=this->m_MatrixP*this->m_VariatesP.get_n_columns(0,which_e_vec);
        p_evecs_factor=this->NormalizeMatrix( p_evecs_factor );
        p_evecs_factor=this->WhitenMatrix(p_evecs_factor);
        p_evecs_factor=p_evecs_factor*p_evecs_factor.transpose();
//        temp=this->m_MatrixQ-p_evecs_factor*this->m_MatrixQ;
//        temp=this->InverseCovarianceMatrix(temp,&this->m_MatrixQ);
//        this->m_MatrixQ=temp;

      }

*/

/*

  bool precond = true;
  MatrixType Cinv;
  if ( precond )
    {
    // Preconditioned Conjugate Gradient Method
    // Tuesday 25 July 2006, by Nadir SOUALEM
    ::ants::antscout << " begin chol " << std::endl;
    MatrixType AAt =  A * A.transpose() ;
    MatrixType kcovmat=this->VNLPseudoInverse( this->m_VariatesP.transpose()*this->m_VariatesP
    vnl_ldl_cholesky chol( AAt );
    ::ants::antscout << " done chol " << std::endl;
    MatrixType chollow = chol.lower_triangle();
    vnl_diag_matrix<double> diag( chol.diagonal() );
    vnl_diag_matrix<double> diaginv( chol.diagonal() );
    for ( unsigned int i = 0; i < diag.cols(); i++ )
      if ( diaginv(i,i) > 1.e-6 )
  {
  diaginv = 1.0 / diaginv( i , i );
  chollow( i , i ) = chollow( i , i ) + diag( i, i );
  }
      else diaginv(i,i) = diag(i,i) = 0;
    ::ants::antscout << " precon " << std::endl;
    // preconditioner
    MatrixType temp = ( chollow * diaginv ) * chollow.transpose();
    Cinv = vnl_matrix_inverse<double>( temp );
    ::ants::antscout << " precon done " << std::endl;
    A = Cinv * A;
    ::ants::antscout << " got A precond " << std::endl;
    }


  bool dosvdinit = true;
  if ( n_vecs > ( this->m_MatrixP.rows() - 1 ) ) dosvdinit = false;
  if ( dosvdinit )
    {
    MatrixType cov = this->m_MatrixP * this->m_MatrixP.transpose();
    vnl_svd<double> qr( cov );
    matrixB = qr.U().extract( this->m_MatrixP.rows(), n_vecs, 0, 0);
    this->m_VariatesP = this->m_MatrixP.transpose( ) * matrixB;
    for( unsigned int i = 0; i < n_vecs; i++ )
      {
      VectorType evec = this->m_VariatesP.get_column( i );
      this->SparsifyP( evec );
      this->m_VariatesP.set_column( i, evec );
      }
    }
  else
    {


*/
