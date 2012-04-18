/*=========================================================================

  Program:   Advanced Normalization Tools
  Module:    $RCSfile: antsSCCANObject.h,v $
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
#ifndef __antsSCCANObject_h
#define __antsSCCANObject_h
// #define EIGEN_DEFAULT_TO_ROW_MAJOR
// #define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET
// #include <Eigen/Dense>
// #include <Eigen/Sparse>
// #include <Eigen/SVD>
// #include "armadillo"
#include <vnl/algo/vnl_matrix_inverse.h>
#include <vnl/algo/vnl_cholesky.h>
#include "itkImageToImageFilter.h"
/** Custom SCCA implemented with vnl and ITK: Flexible positivity constraints, image ops, permutation testing, etc. */
namespace itk
{
namespace ants
{

template <class TInputImage, class TRealType = double>
class ITK_EXPORT antsSCCANObject :
  public ImageToImageFilter<TInputImage, TInputImage>
{
public:
  /** Standard class typdedefs. */
  typedef antsSCCANObject                              Self;
  typedef ImageToImageFilter<TInputImage, TInputImage> Superclass;
  typedef SmartPointer<Self>                           Pointer;
  typedef SmartPointer<const Self>                     ConstPointer;

  /** Method for creation through the object factory. */
  itkNewMacro( Self );

  /** Run-time type information (and related methods). */
  itkTypeMacro( antsSCCANObject, ImageToImageFilter );

  /** Dimension of the images. */
  itkStaticConstMacro( ImageDimension, unsigned int,
                       TInputImage::ImageDimension );

  itkStaticConstMacro( MatrixDimension, unsigned int, 2 );

  /** Typedef support of input types. */
  typedef TInputImage                   ImageType;
  typedef typename ImageType::Pointer   ImagePointer;
  typedef typename ImageType::PixelType PixelType;
  typedef typename ImageType::IndexType IndexType;

  /** Some convenient typedefs. */
  typedef TRealType RealType;
  typedef Image<RealType,
                itkGetStaticConstMacro( ImageDimension )>         RealImageType;

  /** Define eigen types */
  //  typedef Eigen::Matrix<RealType, Eigen::Dynamic, Eigen::Dynamic> eMatrix;
  //  typedef Eigen::Matrix<RealType, Eigen::Dynamic, 1> eVector;
  // typedef Eigen::DynamicSparseMatrix<RealType,Eigen::RowMajor> sMatrix;
  // typedef Eigen::FullPivHouseholderQR<eMatrix> svdobj2;
  // typedef Eigen::JacobiSVD<eMatrix> svdobj;

  /** note, eigen for pseudo-eigenvals  */
  typedef vnl_matrix<RealType>      MatrixType;
  typedef vnl_vector<RealType>      VectorType;
  typedef MatrixType                VariateType;
  typedef vnl_diag_matrix<RealType> DiagonalMatrixType;

  enum SCCANFormulationType { PQ, PminusRQ,  PQminusR,  PminusRQminusR, PQR  };

  /** ivars Set/Get functionality */
  itkSetMacro( MaximumNumberOfIterations, unsigned int );
  itkGetConstMacro( MaximumNumberOfIterations, unsigned int );
  itkSetMacro( MinClusterSizeP, unsigned int );
  itkGetConstMacro( MinClusterSizeP, unsigned int );
  itkSetMacro( MinClusterSizeQ, unsigned int );
  itkGetConstMacro( MinClusterSizeQ, unsigned int );
  itkSetMacro( KeptClusterSize, unsigned int );
  itkGetConstMacro( KeptClusterSize, unsigned int );
  itkSetMacro( AlreadyWhitened, bool );
  itkGetConstMacro( AlreadyWhitened, bool );
  itkSetMacro( ConvergenceThreshold, RealType );
  itkGetConstMacro( ConvergenceThreshold, RealType );
  itkGetConstMacro( CurrentConvergenceMeasurement, RealType );
  itkGetConstMacro( ElapsedIterations, unsigned int );
  itkSetMacro( SCCANFormulation, SCCANFormulationType );
  itkGetConstMacro( SCCANFormulation, SCCANFormulationType );

  void NormalizeWeightsByCovariance(unsigned int);

  void WhitenDataSetForRunSCCANMultiple(unsigned int nvecs = 0);

  void SetPseudoInversePercentVariance( RealType p )
  {
    this->m_PercentVarianceForPseudoInverse = p;
  }

  MatrixType PseudoInverse( MatrixType p_in,  bool take_sqrt = false )
  {
    return this->VNLPseudoInverse(  p_in,  take_sqrt );
  }

  MatrixType VNLPseudoInverse( MatrixType,  bool take_sqrt = false );
  
  void ZeroProduct( VectorType& v1 , VectorType& v2 )
  {
    for ( unsigned int i = 0; i < v1.size(); i++ ) 
      if ( fabs( v2( i ) ) > 0 ) v1( i ) = 0;
  }

  void DeleteRow( MatrixType& , unsigned int );
  void PosNegVector( VectorType& v1 , bool pos  )
  {
    for ( unsigned int i = 0; i < v1.size(); i++ ) 
      if ( v1( i ) < 0 && pos ) v1( i ) = 0;
      else if ( v1( i ) > 0 && !pos ) v1( i ) = 0;
  }

  VectorType Orthogonalize(VectorType Mvec, VectorType V, MatrixType* projecterM = NULL,  MatrixType* projecterV = NULL )
  {
    if( !projecterM && !projecterV )
      {
      double     ipv   = inner_product(V, V);
      if ( ipv == 0 ) return Mvec;
      double     ratio = inner_product(Mvec, V) / ipv;
      VectorType ortho = Mvec - V * ratio;
      return ortho;
      }
    else if( !projecterM &&  projecterV )
      {
      double     ratio = inner_product(Mvec, *projecterV * V) / inner_product(*projecterV * V, *projecterV * V);
      VectorType ortho = Mvec - V * ratio;
      return ortho;
      }
    else if( !projecterV  &&  projecterM )
      {
      double     ratio = inner_product(*projecterM * Mvec, V) / inner_product(V, V);
      VectorType ortho = (*projecterM * Mvec) - V * ratio;
      return ortho;
      }
    else
      {
      double ratio = inner_product(*projecterM * Mvec, *projecterV * V) / inner_product(*projecterV * V,
                                                                                        *projecterV * V);
      VectorType ortho = Mvec - V * ratio;
      for( unsigned int i = 0; i < Mvec.size(); i++ )
        {
        if( Mvec(i) == 0 )
          {
          ortho(i) = 0;
          }
        }
      return ortho;
      }
  }

  MatrixType OrthogonalizeMatrix(MatrixType M, VectorType V )
  {
    for( unsigned int j = 0; j < M.cols(); j++ )
      {
      VectorType Mvec = M.get_column(j);
      double     ratio = inner_product(Mvec, V) / inner_product(V, V);
      VectorType ortho = Mvec - V * ratio;
      M.set_column(j, ortho);
      }
    return M;
  }

  MatrixType RankifyMatrixColumns(MatrixType M )
  {
    RealType rows = (RealType)M.rows();
    for( unsigned long j = 0; j < M.cols(); j++ )
      {
      VectorType Mvec = M.get_column(j);
      VectorType rank = M.get_column(j);
      for( unsigned int i = 0; i < rows; i++ )
        {
        double   rankval = 0;
        RealType xi = Mvec(i);
        for( unsigned int k = 0; k < rows; k++ )
          {
          RealType yi = Mvec(k);
          RealType diff = fabs(xi - yi);
          if( diff > 0 )
            {
            RealType val = (xi - yi) / diff;
            rankval += val;
            }
          }
        rank(i) = rankval / rows;
        }
      M.set_column(j, rank);
      }
    return M;
  }

  itkSetMacro( FractionNonZeroP, RealType );
  itkSetMacro( KeepPositiveP, bool );
  itkGetMacro( KeepPositiveP, bool );
  void SetMaskImageP( ImagePointer mask )
  {
    this->m_MaskImageP = mask;
  }
  void SetMatrixP(  MatrixType matrix )
  {
    this->m_OriginalMatrixP.set_size(matrix.rows(), matrix.cols() );  this->m_MatrixP.set_size(
      matrix.rows(), matrix.cols() ); this->m_OriginalMatrixP.update(matrix); this->m_MatrixP.update(matrix);
  }

  itkSetMacro( FractionNonZeroQ, RealType );
  itkSetMacro( KeepPositiveQ, bool );
  itkGetMacro( KeepPositiveQ, bool );

  void SetMaskImageQ( ImagePointer mask )
  {
    this->m_MaskImageQ = mask;
  }
  void SetMatrixQ(  MatrixType  matrix )
  {
    this->m_OriginalMatrixQ.set_size(matrix.rows(), matrix.cols() );  this->m_MatrixQ.set_size(
      matrix.rows(), matrix.cols() ); this->m_OriginalMatrixQ.update(matrix); this->m_MatrixQ.update(matrix);
  }

  itkSetMacro( FractionNonZeroR, RealType );
  itkSetMacro( KeepPositiveR, bool );
  void SetMaskImageR( ImagePointer mask )
  {
    this->m_MaskImageR = mask;
  }
  void SetMatrixR(  MatrixType matrix )
  {
    this->m_OriginalMatrixR.set_size(matrix.rows(), matrix.cols() );  this->m_MatrixR.set_size(
      matrix.rows(), matrix.cols() ); this->m_OriginalMatrixR.update(matrix); this->m_MatrixR.update(matrix);
  }

  MatrixType GetMatrixP()
  {
    return this->m_MatrixP;
  }
  MatrixType GetMatrixQ()
  {
    return this->m_MatrixQ;
  }
  MatrixType GetMatrixR()
  {
    return this->m_MatrixR;
  }
  MatrixType GetOriginalMatrixP()
  {
    return this->m_OriginalMatrixP;
  }
  MatrixType GetOriginalMatrixQ()
  {
    return this->m_OriginalMatrixQ;
  }
  MatrixType GetOriginalMatrixR()
  {
    return this->m_OriginalMatrixR;
  }

  RealType RunSCCAN2multiple( unsigned int n_vecs );

  RealType RunSCCAN2();

  RealType RunSCCAN3();

  RealType LineSearch( MatrixType& A, VectorType&  x_k, VectorType&  p_k, VectorType&  b, RealType minalph,
                       RealType maxalph,
                       bool );

  RealType EvaluateEnergy( MatrixType& A, VectorType&  x_k, VectorType&  p_k, VectorType&  b, RealType minalph,  bool );

  RealType SparseConjGrad( VectorType &, VectorType, RealType, unsigned int );
  RealType ConjGrad( MatrixType& A, VectorType& x_k, VectorType  b_in, RealType convcrit, unsigned int  );

  RealType SparseNLConjGrad( MatrixType & A,  VectorType & x_k, VectorType  b, RealType, unsigned int, bool keeppos , bool makeprojsparse = false , unsigned int loorth =  0, unsigned int hiorth = 0 );
  void ReSoftThreshold( VectorType& v_in, RealType fractional_goal, bool allow_negative_weights );

  void ConstantProbabilityThreshold( VectorType& v_in, RealType probability_goal, bool allow_negative_weights );

  VectorType InitializeV( MatrixType p, bool random = false);

  MatrixType NormalizeMatrix(MatrixType p);

  /** needed for partial scca */
  MatrixType CovarianceMatrix(MatrixType p, RealType regularization = 1.e-2 )
  {
    if( p.rows() < p.columns() )
      {
      MatrixType invcov = p * p.transpose();
      invcov.set_identity();
      invcov = invcov * regularization + p *p. transpose();
      return invcov;
      }
    else
      {
      MatrixType invcov = p.transpose() * p;
      invcov.set_identity();
      invcov = invcov * regularization + p.transpose() * p;
      return invcov;
      }
  }

  MatrixType WhitenMatrix(MatrixType p, RealType regularization = 1.e-2 )
  {
    double reg = 1.e-9;

    if( p.rows() < p.cols() )
      {
      reg = regularization;
      }
    MatrixType cov = this->CovarianceMatrix(p, reg);
    MatrixType invcov = this->PseudoInverse( cov, true );
    bool       debug = false;
    if( debug )
      {
	::ants::antscout << " cov " << std::endl;   ::ants::antscout << cov << std::endl;
	::ants::antscout << " invcov " << std::endl;   ::ants::antscout << invcov << std::endl;
	::ants::antscout << " id? " << std::endl;   ::ants::antscout << cov * invcov << std::endl;
      }
    if( p.rows() < p.columns() )
      {
      return invcov * p;
      }
    else
      {
      return p * invcov;
      }
  }


  MatrixType WhitenMatrixByAnotherMatrix(MatrixType p, MatrixType op, RealType regularization = 1.e-2)
  {
    MatrixType invcov = this->CovarianceMatrix(op, regularization);

    invcov = this->PseudoInverse( invcov, true );
    if( p.rows() < p.columns() )
      {
      return invcov * p;
      }
    else
      {
      return p * invcov;
      }
  }

  MatrixType ProjectionMatrix(MatrixType b, double regularization = 0.001)
  {
    bool armadillo = false;

    b = this->NormalizeMatrix( b );
    MatrixType mat = b * b.transpose();
    if( !armadillo )
      {
      MatrixType cov( mat.rows(), mat.cols(), 0);
      cov.set_identity();
      mat = cov * regularization + mat;
      return vnl_svd<double>( mat ).inverse();
      //      return vnl_svd<double>( mat ).pinverse( mc );
      }
    else
      {   /*
      arma::mat amat( b.rows(), b.rows());
      for ( unsigned int i = 0 ; i < b.rows(); i++ )
  for ( unsigned int j = 0 ; j < b.rows(); j++ )
    {
    amat( i , j ) = mat( i , j );
    if ( i == j ) amat( i , j ) += regularization;
    }
      arma::mat invamat = arma::inv( amat , 1.e-2 );

      for ( unsigned int i = 0 ; i < b.rows(); i++ )
  for ( unsigned int j = 0 ; j < b.rows(); j++ ) mat( i , j ) = invamat( i , j );
       */
      return mat;
      }
  }

  VectorType TrueCCAPowerUpdate(RealType penaltyP, MatrixType p, VectorType w_q, MatrixType q, bool keep_pos,
                                bool factorOutR);

  MatrixType PartialOutZ( MatrixType /*X*/, MatrixType /*Y*/, MatrixType /*Z*/ )
  {
    ::ants::antscout << "ERROR:  This function not yet implemented." << std::endl;
    /** compute the effect of Z and store it for later use */
  }

  VectorType GetPWeights()
  {
    return this->m_WeightsP;
  }
  VectorType GetQWeights()
  {
    return this->m_WeightsQ;
  }
  VectorType GetRWeights()
  {
    return this->m_WeightsR;
  }
  RealType GetCorrelationForSignificanceTest()
  {
    return this->CorrelationForSignificanceTest;
  }

  VectorType GetCanonicalCorrelations()
  {
    return this->m_CanonicalCorrelations;
  }

  VectorType GetVariateP( unsigned int i = 0 )
  {
    return this->m_VariatesP.get_column(i);
  }

  VectorType GetVariateQ( unsigned int i = 0 )
  {
    return this->m_VariatesQ.get_column(i);
  }

  MatrixType GetVariatesP()
  {
    return this->m_VariatesP;
  }

  MatrixType GetVariatesQ()
  {
    return this->m_VariatesQ;
  }

  RealType ComputeEnergySlope( std::vector<RealType> vexlist, unsigned int n )
  {
    unsigned int N = vexlist.size();
    unsigned int loline = N - n;

    if( N < n * 2 )
      {
      return 1;
      }
    double s0 = (n + 1);
    double s1 =  0;
    double s2 =  0;
    double t0 =  0;
    double t1 =  0;
    for( unsigned int i = loline; i < N; ++i )
      {
      const double t = (i - loline);
      s1 += t;
      s2 += (t * t);
      const double e = vexlist[i] - vexlist[loline];
      t0 += e;
      t1 += (e * e);
      }
    double M = 1;
    double denom = (s0 * s2 - s1 * s1);
    if( denom > 0 )
      {
      M = ( s1 * t0 - s0 * t1 ) / denom;
      }
    return M;
    /*
    std::vector<RealType> sublist;
    for (int i=listsize-4; i<listsize; i++) sublist.push_back( vexlist[i] );
    bool allequal=true;
    for (int i = 4; i < listsize; ++i) {
      allequal=true;
      for (int j=0; j<4; j++) if ( sublist[j] != vexlist[i-j] ) allequal=false;
      if (allequal) return 1.e-7;
    }
    return sts/stt*(1);
*/
  }

  RealType SparseCCA(unsigned int nvecs);

  RealType SparsePartialCCA(unsigned int nvecs);

  RealType SparsePartialArnoldiCCA(unsigned int nvecs);

  RealType SparseArnoldiSVDGreedy(unsigned int nvecs);

  RealType SparseArnoldiSVD(unsigned int nvecs);

  RealType SparseArnoldiSVD_x(unsigned int nvecs);

  RealType SparseArnoldiSVD_z(unsigned int nvecs);

  RealType ComputeSPCAEigenvalues(unsigned int, RealType, bool );
  RealType BasicSVD();

  RealType CGSPCA(unsigned int nvecs);

  RealType NetworkDecomposition(unsigned int nvecs);

  MatrixType GetCovMatEigenvectors( MatrixType p );

protected:

  void SortResults(unsigned int n_vecs);

// for pscca
  void UpdatePandQbyR();

  void SparsifyP( VectorType& x_k1, bool keeppos )
  {
    bool negate = false;

    if( x_k1.max_value() <= 0 )
      {
      negate = true;
      }
    if( negate )
      {
      x_k1 = x_k1 * ( -1 );
      }
    RealType fnp = this->m_FractionNonZeroP;
    this->ReSoftThreshold( x_k1, fnp, keeppos );
    this->ClusterThresholdVariate( x_k1, this->m_MaskImageP, this->m_MinClusterSizeP );
    if( negate )
      {
      x_k1 = x_k1 * ( -1 );
      }
  }

  void SparsifyP( VectorType& x_k1, VectorType& refvec  )
  {
    if( x_k1.size() != refvec.size() )
      {
      ::ants::antscout << " sizes dont match " << std::endl; std::exception();
      }
    for( unsigned int i = 0; i < x_k1.size(); i++ )
      {
      if( refvec(i) == 0 )
        {
        x_k1(i) = 0;
        }
      }
  }

  MatrixType  DeleteCol( MatrixType p_in, unsigned int col)
  {
    unsigned int ncols = p_in.cols() - 1;

    if( col >= ncols )
      {
      ncols = p_in.cols();
      }
    MatrixType   p(p_in.rows(), ncols);
    unsigned int colct = 0;
    for( long i = 0; i < p.cols(); ++i ) // loop over cols
      {
      if( i != col )
        {
        p.set_column(colct, p_in.get_column(i) );
        colct++;
        }
      }
    return p;
  }

  RealType CountNonZero( VectorType v )
  {
    unsigned long ct = 0;
    for( unsigned int i = 0; i < v.size(); i++ )
      {
      if( v[i] != 0 )
        {
        ct++;
        }
      }
    return (RealType)ct / (RealType)v.size();
  }

  RealType PearsonCorr(VectorType v1, VectorType v2 )
  {
    double xysum = 0;
    for( unsigned int i = 0; i < v1.size(); i++ )
      {
      xysum += v1(i) * v2(i);
      }
    double frac = 1.0 / (double)v1.size();
    double xsum = v1.sum(), ysum = v2.sum();
    double xsqr = v1.squared_magnitude();
    double ysqr = v2.squared_magnitude();
    double numer = xysum - frac * xsum * ysum;
    double denom = sqrt( ( xsqr - frac * xsum * xsum) * ( ysqr - frac * ysum * ysum) );
    if( denom <= 0 )
      {
      return 0;
      }
    return numer / denom;
  }

  RealType GoldenSection( MatrixType& A, VectorType&  x_k, VectorType&  p_k, VectorType&  bsol, RealType minalph,
                          RealType maxalph, bool keeppos, RealType a, RealType b, RealType c,
                          RealType tau );

  //  VectorType vEtoV( eVector v ) {
  //   VectorType v_out( v.data() , v.size() );
  //  return v_out;
  // }

  // eVector vVtoE( VectorType v ) {
  //  eVector v_out( v.size() );
  //  for (unsigned int i=0; i < v.size() ; i++) v_out(i)=v(i);
  //  return v_out;
  // }
  /*
  MatrixType mEtoV( eMatrix m , unsigned int ncols = 0) {
    MatrixType m_out( m.data() , m.rows() , m.cols() );
    if (  m(0,1) != m_out(0,1) ) {
      ::ants::antscout << " WARNING!! in eigen to vnl coversion for matrices " << std::endl;
      ::ants::antscout <<" eigen " << m(0,1) << " vnl " << m_out(0,1) << std::endl;
    }
    //    ::ants::antscout <<" eigen at (0,1) " << m(0,1) << " vnl at (0,1) " << m_out(0,1) <<  " vnl at (1,0) " << m_out(1,0)  << std::endl;
    if ( ncols == 0 )
      return m_out;
    else return (m_out).get_n_columns(0,ncols);
    // use this if you dont set #define EIGEN_DEFAULT_TO_ROW_MAJOR (we do this)
    if ( ncols == 0 )
      return m_out.transpose();
    else return (m_out.transpose()).get_n_columns(0,ncols);
    }*/
  /*
  eMatrix mVtoE( MatrixType m ) {
// NOTE: Eigen matrices are the transpose of vnl matrices unless you set #define EIGEN_DEFAULT_TO_ROW_MAJOR which we do
    eMatrix m_out(m.rows(),m.cols());
    for ( long i=0; i<m.rows(); ++i)
      for ( long j=0; j<m.cols(); ++j)
    m_out(i,j)=m(i,j);
    return m_out;
   }
  */
  antsSCCANObject();
  ~antsSCCANObject()
  {
  }

  void PrintSelf( std::ostream &, /* os */ Indent /* indent */) const
  {
    if( this->m_MaskImageP && this->m_MaskImageQ && this->m_MaskImageR )
      {
      ::ants::antscout << " 3 matrices " << std::endl;
      }
    else if( this->m_MaskImageP && this->m_MaskImageQ  )
      {
      ::ants::antscout << " 2 matrices " << std::endl;
      }
    else
      {
      ::ants::antscout << " fewer than 2 matrices " << std::endl;
      }
  }

  void RunDiagnostics(unsigned int);

  void AddColumnsToMatrix( MatrixType& mat_to_add_to , MatrixType& mat_to_take_from , unsigned int col0 , unsigned int coln )
    {
      MatrixType outmat( mat_to_add_to.rows() ,  mat_to_add_to.cols() +  ( coln - col0 ) + 1 , 0 );
      for ( unsigned int i = 0; i < mat_to_add_to.cols(); i++ ) outmat.set_column( i ,  mat_to_add_to.get_column( i ) );
      unsigned int ct = mat_to_add_to.cols();
      for ( unsigned int i = col0; i <= coln; i++ )
	{
	outmat.set_column( ct ,   mat_to_take_from.get_column( i ) );
	ct++;
	}
      mat_to_add_to = outmat;
    }

private:

  ImagePointer ConvertVariateToSpatialImage( VectorType variate, ImagePointer mask, bool threshold_at_zero = false );

  VectorType ClusterThresholdVariate( VectorType &, ImagePointer mask, unsigned int);

  bool       m_Debug;
  MatrixType m_OriginalMatrixP;
  MatrixType m_OriginalMatrixQ;
  MatrixType m_OriginalMatrixR;

  antsSCCANObject(const Self &); // purposely not implemented
  void operator=(const Self &);  // purposely not implemented

  unsigned int m_ElapsedIterations;
  unsigned int m_MaximumNumberOfIterations;
  RealType     m_CurrentConvergenceMeasurement;
  RealType     m_ConvergenceThreshold;

  SCCANFormulationType m_SCCANFormulation;
  RealType             m_PinvTolerance;
  RealType             m_PercentVarianceForPseudoInverse;
  RealType             m_Epsilon; /** used to prevent div by zero */

  VectorType   m_WeightsP;
  MatrixType   m_MatrixP;
  ImagePointer m_MaskImageP;
  RealType     m_FractionNonZeroP;
  bool         m_KeepPositiveP;

  VectorType   m_WeightsQ;
  MatrixType   m_MatrixQ;
  ImagePointer m_MaskImageQ;
  RealType     m_FractionNonZeroQ;
  bool         m_KeepPositiveQ;

  MatrixType  m_Eigenvectors;
  VectorType  m_Eigenvalues;
  VectorType  m_CanonicalCorrelations;
  VariateType m_SparseVariatesP;
  VariateType m_VariatesP;
  VariateType m_VariatesQ;

  VectorType   m_WeightsR;
  MatrixType   m_MatrixR;
  ImagePointer m_MaskImageR;
  RealType     m_FractionNonZeroR;
  bool         m_KeepPositiveR;
/** a special variable for pscca, holds R^T R */
  MatrixType m_MatrixRRt;
  MatrixType m_MatrixRp;
  MatrixType m_MatrixRq;

  bool     m_AlreadyWhitened;
  bool     m_SpecializationForHBM2011;
  RealType m_CorrelationForSignificanceTest;
  RealType m_Intercept;

  unsigned int              m_MinClusterSizeP;
  unsigned int              m_MinClusterSizeQ;
  unsigned int              m_KeptClusterSize;
  unsigned int              m_GoldenSectionCounter;
  VectorType                m_ClusterSizes;
  vnl_diag_matrix<RealType> m_Indicator;

};

} // namespace ants
} // namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#include "antsSCCANObject.hxx"
#endif

#endif
