#ifndef __itkantsRegistrationHelper_hxx
#define __itkantsRegistrationHelper_hxx
#include <string>

#include "itkantsRegistrationHelper.h"
#include "itkANTSNeighborhoodCorrelationImageToImageMetricv4.h"
#include "itkMeanSquaresImageToImageMetricv4.h"
#include "itkCorrelationImageToImageMetricv4.h"

#include "itkAffineTransform.h"
#include "itkANTSAffine3DTransform.h"
#include "itkANTSCenteredAffine2DTransform.h"
#include "itkCompositeTransform.h"
#include "itkIdentityTransform.h"
#include "itkEuler2DTransform.h"
#include "itkEuler3DTransform.h"
#include "itkVersorRigid3DTransform.h"
#include "itkQuaternionRigidTransform.h"
#include "itkSimilarity2DTransform.h"
#include "itkSimilarity3DTransform.h"
#include "itkMatrixOffsetTransformBase.h"
#include "itkTranslationTransform.h"
#include "itkMatrixOffsetTransformBase.h"
#include "itkTransform.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkDisplacementFieldTransform.h"
#include "itkTransformFactory.h"
#include "itkTransformFileReader.h"
#include "itkTransformFileWriter.h"
#include "itkImageRegistrationMethodv4.h"
#include "itkImageToHistogramFilter.h"
#include "itkHistogramMatchingImageFilter.h"
#include "itkIntensityWindowingImageFilter.h"
#include "itkMattesMutualInformationImageToImageMetricv4.h"
#include "itkBSplineTransform.h"
#include "itkBSplineSmoothingOnUpdateDisplacementFieldTransform.h"
#include "itkDisplacementFieldTransform.h"
#include "itkGaussianSmoothingOnUpdateDisplacementFieldTransform.h"
#include "itkTimeVaryingVelocityFieldImageRegistrationMethodv4.h"
#include "itkTimeVaryingBSplineVelocityFieldImageRegistrationMethod.h"
#include "itkSyNImageRegistrationMethod.h"
#include "itkBSplineTransformParametersAdaptor.h"
#include "itkBSplineSmoothingOnUpdateDisplacementFieldTransformParametersAdaptor.h"
#include "itkGaussianSmoothingOnUpdateDisplacementFieldTransformParametersAdaptor.h"
#include "itkTimeVaryingVelocityFieldTransformParametersAdaptor.h"
#include "itkTimeVaryingBSplineVelocityFieldTransformParametersAdaptor.h"
#include "itkTimeProbe.h"
#include "itkCommand.h"

namespace itk
{
namespace ants
{

template <class TFilter>
class CommandIterationUpdate : public itk::Command
{
public:
  typedef CommandIterationUpdate  Self;
  typedef itk::Command            Superclass;
  typedef itk::SmartPointer<Self> Pointer;
  itkNewMacro( Self );
protected:
  CommandIterationUpdate()
  {
  };
public:

  void Execute(itk::Object *caller, const itk::EventObject & event)
  {
    Execute( (const itk::Object *) caller, event);
  }

  void Execute(const itk::Object * object, const itk::EventObject & event)
  {
    TFilter * filter = const_cast<TFilter *>( dynamic_cast<const TFilter *>( object ) );

    unsigned int currentLevel = 0;

    if( typeid( event ) == typeid( itk::IterationEvent ) )
      {
      currentLevel = filter->GetCurrentLevel() + 1;
      }
    if( currentLevel < this->m_NumberOfIterations.size() )
      {
      typename TFilter::ShrinkFactorsArrayType shrinkFactors = filter->GetShrinkFactorsPerLevel();
      typename TFilter::SmoothingSigmasArrayType smoothingSigmas = filter->GetSmoothingSigmasPerLevel();
      typename TFilter::TransformParametersAdaptorsContainerType adaptors =
        filter->GetTransformParametersAdaptorsPerLevel();

      std::cout << "  Current level = " << currentLevel << std::endl;
      std::cout << "    number of iterations = " << this->m_NumberOfIterations[currentLevel] << std::endl;
      std::cout << "    shrink factors = " << shrinkFactors[currentLevel] << std::endl;
      std::cout << "    smoothing sigmas = " << smoothingSigmas[currentLevel] << std::endl;
      std::cout << "    required fixed parameters = " << adaptors[currentLevel]->GetRequiredFixedParameters()
                << std::endl;

      typedef itk::GradientDescentOptimizerv4 GradientDescentOptimizerType;
      GradientDescentOptimizerType * optimizer = reinterpret_cast<GradientDescentOptimizerType *>(
          const_cast<typename TFilter::OptimizerType *>( filter->GetOptimizer() ) );
      optimizer->SetNumberOfIterations( this->m_NumberOfIterations[currentLevel] );
      }
  }

  void SetNumberOfIterations( std::vector<unsigned int> iterations )
  {
    this->m_NumberOfIterations = iterations;
  }

private:
  std::vector<unsigned int> m_NumberOfIterations;
};

//
// Transform traits to generalize the rigid transform
//
template <unsigned int ImageDimension>
class RigidTransformTraits
{
// Don't worry about the fact that the default option is the
// affine Transform, that one will not actually be instantiated.
public:
  typedef itk::AffineTransform<double, ImageDimension> TransformType;
};

template <>
class RigidTransformTraits<2>
{
public:
  typedef itk::Euler2DTransform<double> TransformType;
};

template <>
class RigidTransformTraits<3>
{
public:
  // typedef itk::VersorRigid3DTransform<double> TransformType;
  // typedef itk::QuaternionRigidTransform<double>  TransformType;
  typedef itk::Euler3DTransform<double> TransformType;
};

template <unsigned int ImageDimension>
class SimilarityTransformTraits
{
// Don't worry about the fact that the default option is the
// affine Transform, that one will not actually be instantiated.
public:
  typedef itk::AffineTransform<double, ImageDimension> TransformType;
};

template <>
class SimilarityTransformTraits<2>
{
public:
  typedef itk::Similarity2DTransform<double> TransformType;
};

template <>
class SimilarityTransformTraits<3>
{
public:
  typedef itk::Similarity3DTransform<double> TransformType;
};

template <unsigned int ImageDimension>
class CompositeAffineTransformTraits
{
// Don't worry about the fact that the default option is the
// affine Transform, that one will not actually be instantiated.
public:
  typedef itk::AffineTransform<double, ImageDimension> TransformType;
};
template <>
class CompositeAffineTransformTraits<2>
{
public:
  typedef itk::ANTSCenteredAffine2DTransform<double> TransformType;
};
template <>
class CompositeAffineTransformTraits<3>
{
public:
  typedef itk::ANTSAffine3DTransform<double> TransformType;
};

template <class ImageType>
typename ImageType::Pointer PreprocessImage( ImageType * inputImage,
                                             typename ImageType::PixelType lowerScaleValue,
                                             typename ImageType::PixelType upperScaleValue,
                                             float winsorizeLowerQuantile, float winsorizeUpperQuantile,
                                             ImageType *histogramMatchSourceImage = NULL )
{
  typedef Statistics::ImageToHistogramFilter<ImageType>   HistogramFilterType;
  typedef typename HistogramFilterType::InputBooleanObjectType InputBooleanObjectType;
  typedef typename HistogramFilterType::HistogramSizeType      HistogramSizeType;
  typedef typename HistogramFilterType::HistogramType          HistogramType;

  HistogramSizeType histogramSize( 1 );
  histogramSize[0] = 256;

  typename InputBooleanObjectType::Pointer autoMinMaxInputObject = InputBooleanObjectType::New();
  autoMinMaxInputObject->Set( true );

  typename HistogramFilterType::Pointer histogramFilter = HistogramFilterType::New();
  histogramFilter->SetInput( inputImage );
  histogramFilter->SetAutoMinimumMaximumInput( autoMinMaxInputObject );
  histogramFilter->SetHistogramSize( histogramSize );
  histogramFilter->SetMarginalScale( 10.0 );
  histogramFilter->Update();

  float lowerValue = histogramFilter->GetOutput()->Quantile( 0, winsorizeLowerQuantile );
  float upperValue = histogramFilter->GetOutput()->Quantile( 0, winsorizeUpperQuantile );

  typedef itk::IntensityWindowingImageFilter<ImageType, ImageType> IntensityWindowingImageFilterType;

  typename IntensityWindowingImageFilterType::Pointer windowingFilter = IntensityWindowingImageFilterType::New();
  windowingFilter->SetInput( inputImage );
  windowingFilter->SetWindowMinimum( lowerValue );
  windowingFilter->SetWindowMaximum( upperValue );
  windowingFilter->SetOutputMinimum( lowerScaleValue );
  windowingFilter->SetOutputMaximum( upperScaleValue );
  windowingFilter->Update();

  typename ImageType::Pointer outputImage = NULL;
  if( histogramMatchSourceImage )
    {
    typedef HistogramMatchingImageFilter<ImageType, ImageType> HistogramMatchingFilterType;
    typename HistogramMatchingFilterType::Pointer matchingFilter = HistogramMatchingFilterType::New();
    matchingFilter->SetSourceImage( windowingFilter->GetOutput() );
    matchingFilter->SetReferenceImage( histogramMatchSourceImage );
    matchingFilter->SetNumberOfHistogramLevels( 256 );
    matchingFilter->SetNumberOfMatchPoints( 12 );
    matchingFilter->ThresholdAtMeanIntensityOn();
    matchingFilter->Update();

    outputImage = matchingFilter->GetOutput();
    outputImage->Update();
    outputImage->DisconnectPipeline();
    }
  else
    {
    outputImage = windowingFilter->GetOutput();
    outputImage->Update();
    outputImage->DisconnectPipeline();
    }

  return outputImage;
}

void
RegistrationHelper
::AddInitialTransform(const std::string &filename, bool useInverse)
{
  InitialTransform init(filename,useInverse);
  this->m_InitialTransforms.push_back(init);
}

RegistrationHelper::MetricType 
RegistrationHelper
::StringToMetricType(const std::string &str)
{
  if(str == "cc")
    {
    return CC;
    }
  else if(str == "mi")
    {
    return MI;
    }
  // else if(str = "mi2")
  //   {
  //   return MI2;
  //   }
  else if(str == "msq")
    {
    return MeanSquares;
    }
  
}

void
RegistrationHelper
::AddMetric(MetricType metricType,
           const std::string fixedImage,
           const std::string movingImage,
           double weighting,
           SamplingStrategy samplingStrategy,
           int numberOfBins,
            double radius,
            double samplingPercentage)
{
  Metric init(metricType,fixedImage,movingImage,
              weighting,samplingStrategy,numberOfBins,
              radius,
              samplingPercentage);
  this->m_Metrics.push_back(init);
}

void
RegistrationHelper
::AddRigidTransform(double GradientStep)
{
  TransformMethod init;
  init.m_XfrmMethod = Rigid;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddAffineTransform(double GradientStep)
{
  TransformMethod init;
  init.m_XfrmMethod = Affine;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddCompositeAffineTransform(double GradientStep)
{
  TransformMethod init;
  init.m_XfrmMethod = CompositeAffine;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddSimilarityTransform(double GradientStep)
{
  TransformMethod init;
  init.m_XfrmMethod = Similarity;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddTranslationTransform(double GradientStep)
{
  TransformMethod init;
  init.m_XfrmMethod = Translation;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddBSplineTransform(double GradientStep,std::vector<unsigned int> &MeshSizeAtBaseLevel)
{
  TransformMethod init;
  init.m_XfrmMethod = BSpline;
  init.m_GradientStep = GradientStep;
  init.m_MeshSizeAtBaseLevel = MeshSizeAtBaseLevel;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddGaussianDisplacementFieldTransform(double GradientStep, double UpdateFieldSigmaInPhysicalSpace)
{
  TransformMethod init;
  init.m_XfrmMethod = GaussianDisplacementField;
  init.m_GradientStep = GradientStep;
  init.m_UpdateFieldSigmaInPhysicalSpace = UpdateFieldSigmaInPhysicalSpace;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddBSplineDisplacementFieldTransform(double GradientStep,
                                       std::vector<unsigned int> &UpdateFieldMeshSizeAtBaseLevel,
                                       std::vector<unsigned int> &TotalFieldMeshSizeAtBaseLevel,
                                       unsigned int SplineOrder)
{
  TransformMethod init;
  init.m_XfrmMethod = BSplineDisplacementField;
  init.m_GradientStep = GradientStep;
  init.m_UpdateFieldMeshSizeAtBaseLevel = UpdateFieldMeshSizeAtBaseLevel;
  init.m_TotalFieldMeshSizeAtBaseLevel = TotalFieldMeshSizeAtBaseLevel;
  init.m_SplineOrder = SplineOrder;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddTimeVaryingVelocityFieldTransform(double GradientStep,
                                       unsigned int NumberOfTimeIndices,
                                       double UpdateFieldSigmaInPhysicalSpace,
                                       double UpdateFieldTimeSigma,
                                       double TotalFieldSigmaInPhysicalSpace,
                                       double TotalFieldTimeSigma)
{
  TransformMethod init;
  init.m_XfrmMethod = TimeVaryingVelocityField;
  init.m_GradientStep = GradientStep;
  init.m_NumberOfTimeIndices = NumberOfTimeIndices;
  init.m_UpdateFieldSigmaInPhysicalSpace = UpdateFieldSigmaInPhysicalSpace;
  init.m_UpdateFieldTimeSigma = UpdateFieldTimeSigma;
  init.m_TotalFieldSigmaInPhysicalSpace = TotalFieldSigmaInPhysicalSpace;
  init.m_TotalFieldTimeSigma = TotalFieldTimeSigma;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddTimeVaryingBSplineVelocityFieldTransform(double GradientStep, std::vector<unsigned int> VelocityFieldMeshSize,
                                              unsigned int NumberOfTimePointSamples, unsigned int SplineOrder)
{
  TransformMethod init;
  init.m_XfrmMethod = TimeVaryingBSplineVelocityField;;
  init.m_GradientStep = GradientStep;
  init.m_VelocityFieldMeshSize = VelocityFieldMeshSize;
  init.m_NumberOfTimePointSamples = NumberOfTimePointSamples;
  init.m_SplineOrder = SplineOrder;
  this->m_TransformMethods.push_back(init);
}

void
RegistrationHelper
::AddSyNTransform(double GradientStep,double UpdateFieldSigmaInPhysicalSpace, double TotalFieldSigmaInPhysicalSpace)
{
  TransformMethod init;
  init.m_XfrmMethod = Syn;
  init.m_GradientStep = GradientStep;
  this->m_TransformMethods.push_back(init);
  init.m_UpdateFieldSigmaInPhysicalSpace = UpdateFieldSigmaInPhysicalSpace;
  init.m_TotalFieldSigmaInPhysicalSpace = TotalFieldSigmaInPhysicalSpace;
}

void
RegistrationHelper
::SetIterations(const std::vector<std::vector<unsigned int> > &Iterations)
{
  this->m_Iterations = Iterations;
}

void
RegistrationHelper
::SetSmoothingSigmas(const std::vector<std::vector<float> > &SmoothingSigmas)
{
  this->m_SmoothingSigmas = SmoothingSigmas;
}

void
RegistrationHelper
::SetShrinkFactors(const std::vector<std::vector<unsigned int> > &ShrinkFactors)
{
  this->m_ShrinkFactors = ShrinkFactors;
}

void
RegistrationHelper
::SetWinsorizeImageIntensities(bool Winsorize, float LowerQuantile, float UpperQuantile)
{
  this->m_WinsorizeImageIntensities = Winsorize;
  this->m_LowerQuantile = LowerQuantile;
  this->m_UpperQuantile = UpperQuantile;
}

int
RegistrationHelper
::ValidateParameters()
{
  this->m_NumberOfStages = this->m_TransformMethods.size();
  if(this->m_NumberOfStages == 0)
    {
    std::cerr << "No transformations are specified." << std::endl;
    return EXIT_FAILURE;
    }
  if(this->m_Metrics.size() != this->m_NumberOfStages)
    {
    std::cerr << "The number of metrics specified does not match the number of stages." << std::endl;
    return EXIT_FAILURE;
    }
  if(this->m_Iterations.size() != this->m_NumberOfStages)
    {
    std::cerr << "The number of iteration sets specified does not match the number of stages." << std::endl;
    return EXIT_FAILURE;
    }
  if(this->m_ShrinkFactors.size() != this->m_NumberOfStages)
    {
    std::cerr << "The number of shrinkFactors specified does not match the number of stages." << std::endl;
    return EXIT_FAILURE;
    }
  if(this->m_SmoothingSigmas.size() != this->m_NumberOfStages)
    {
    std::cerr << "The number of smoothing sigma sets specified does not match the number of stages." << std::endl;
    return EXIT_FAILURE;
    }
  if(this->m_OutputTransformPrefix == "")
    {
    std::cerr << "Output option not specified." << std::endl;
    return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

template <unsigned VImageDimension, class TCompositeTransform>
int
RegistrationHelper
::SetupInitialTransform(typename TCompositeTransform::Pointer &compositeTransform)
{
  typedef double                                RealType;

  // Register the matrix offset transform base class to the
  // transform factory for compatibility with the current ANTs.
  typedef itk::MatrixOffsetTransformBase<double, VImageDimension, VImageDimension> MatrixOffsetTransformType;
  itk::TransformFactory<MatrixOffsetTransformType>::RegisterTransform();

  // Load an identity transform in case no transforms are loaded.
  typedef itk::IdentityTransform<RealType, VImageDimension> IdentityTransformType;
  typename IdentityTransformType::Pointer identityTransform = IdentityTransformType::New();
  compositeTransform->AddTransform( identityTransform );
  compositeTransform->SetAllTransformsToOptimize( false );
  if(this->m_InitialTransforms.size() == 0)
    {
    return EXIT_SUCCESS;
    }
  // loop through list
  for( unsigned int n = 0; n < m_InitialTransforms.size(); n++ )
    {
    std::string initialTransformName;
    std::string initialTransformType;

    typedef itk::Transform<double, VImageDimension, VImageDimension> TransformType;
    typename TransformType::Pointer initialTransform;

    bool hasTransformBeenRead = false;
    initialTransformName = this->m_InitialTransforms[n].m_Filename;

    typedef itk::DisplacementFieldTransform<double, VImageDimension> DisplacementFieldTransformType;

    typedef typename DisplacementFieldTransformType::DisplacementFieldType DisplacementFieldType;

    typedef itk::ImageFileReader<DisplacementFieldType> DisplacementFieldReaderType;
    typename DisplacementFieldReaderType::Pointer fieldReader =
      DisplacementFieldReaderType::New();
    try
      {
      fieldReader->SetFileName( initialTransformName.c_str() );
      fieldReader->Update();
      hasTransformBeenRead = true;
      }
    catch( ... )
      {
      hasTransformBeenRead = false;
      }

    if(hasTransformBeenRead)
      {
      typename DisplacementFieldTransformType::Pointer displacementFieldTransform =
        DisplacementFieldTransformType::New();
      displacementFieldTransform->SetDisplacementField( fieldReader->GetOutput() );
      initialTransform = dynamic_cast<TransformType *>( displacementFieldTransform.GetPointer() );
      }
    else
      {
      typedef TransformFileReader TransformReaderType;
      typename TransformReaderType::Pointer initialTransformReader
        = TransformReaderType::New();

      initialTransformReader->SetFileName( initialTransformName.c_str() );
      try
        {
        initialTransformReader->Update();
        }
      catch( const itk::ExceptionObject & e )
        {
        std::cerr << "Transform reader for "
                  << initialTransformName << " caught an ITK exception:\n";
        e.Print( std::cerr );
        return EXIT_FAILURE;
        }
      catch( const std::exception & e )
        {
        std::cerr << "Transform reader for "
                  << initialTransformName << " caught an exception:\n";
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
        }
      catch( ... )
        {
        std::cerr << "Transform reader for "
                  << initialTransformName << " caught an unknown exception!!!\n";
        return EXIT_FAILURE;
        }

      initialTransform =
        dynamic_cast<TransformType *>( ( ( initialTransformReader->GetTransformList() )->front() ).GetPointer() );

      if(this->m_InitialTransforms[n].m_UseInverse)
        {
        initialTransform = dynamic_cast<TransformType *>(initialTransform->GetInverseTransform().GetPointer() );
        if( initialTransform.IsNull() )
          {
          std::cerr << "Inverse does not exist for " << initialTransformName
                    << std::endl;
          return EXIT_FAILURE;
          }
        }
      compositeTransform->AddTransform( initialTransform );
      }
    }
  return EXIT_SUCCESS;
}

template <unsigned VImageDimension>
int
RegistrationHelper
::DoRegistrationInternal()
{
  if(this->ValidateParameters() != EXIT_SUCCESS)
    {
    return EXIT_FAILURE;
    }

  typedef double                                RealType;
  typedef float                                 PixelType;
  typedef itk::Image<PixelType, VImageDimension> ImageType;
  typedef itk::CompositeTransform<RealType, VImageDimension> CompositeTransformType;
  typename CompositeTransformType::Pointer compositeTransform = CompositeTransformType::New();


  // Load an initial initialTransform if requested


  if(this->SetupInitialTransform<VImageDimension,CompositeTransformType>(compositeTransform) != EXIT_SUCCESS)
    {
    return EXIT_FAILURE;
    }

  size_t numberOfInitialTransforms = compositeTransform->GetNumberOfTransforms();

  for( int currentStage = this->m_NumberOfStages - 1; currentStage >= 0; currentStage-- )
    {
    itk::TimeProbe timer;
    timer.Start();

    typedef itk::ImageRegistrationMethodv4<ImageType, ImageType> AffineRegistrationType;

    std::cout << std::endl << "Stage "
              << ( numberOfInitialTransforms + this->m_NumberOfStages - currentStage - 1 ) << std::endl;
    std::stringstream currentStageString;
    currentStageString << ( numberOfInitialTransforms + this->m_NumberOfStages - currentStage - 1 );

    // Get the fixed and moving images

    std::string fixedImageFileName = this->m_Metrics[currentStage].m_FixedImage;
    std::string movingImageFileName = this->m_Metrics[currentStage].m_MovingImage;

    std::cout << "  fixed image: " << fixedImageFileName << std::endl;
    std::cout << "  moving image: " << movingImageFileName << std::endl;

    typedef itk::ImageFileReader<ImageType> ImageReaderType;
    typename ImageReaderType::Pointer fixedImageReader = ImageReaderType::New();
    fixedImageReader->SetFileName( fixedImageFileName.c_str() );
    fixedImageReader->Update();
    typename ImageType::Pointer fixedImage = fixedImageReader->GetOutput();
    try
      {
      fixedImage->Update();
      }
    catch( itk::ExceptionObject & excp )
      {
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    fixedImage->DisconnectPipeline();

    typename ImageReaderType::Pointer movingImageReader = ImageReaderType::New();
    movingImageReader->SetFileName( movingImageFileName.c_str() );
    movingImageReader->Update();
    typename ImageType::Pointer movingImage = movingImageReader->GetOutput();
    try
      {
      movingImage->Update();
      }
    catch( itk::ExceptionObject & excp )
      {
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    movingImage->DisconnectPipeline();

    // Preprocess images

    std::string outputPreprocessingString = "";

    PixelType lowerScaleValue = 0.0;
    PixelType upperScaleValue = 1.0;


    typename ImageType::Pointer preprocessFixedImage =
      PreprocessImage<ImageType>( fixedImage, lowerScaleValue, upperScaleValue, this->m_LowerQuantile, this->m_UpperQuantile,
                                  NULL );

    typename ImageType::Pointer preprocessMovingImage;
    if( this->m_UseHistogramMatching )
      {
      preprocessMovingImage =
        PreprocessImage<ImageType>( movingImage,
                                    lowerScaleValue, upperScaleValue,
                                    this->m_LowerQuantile, this->m_UpperQuantile,
                                    preprocessFixedImage );
      }
    else
      {
      preprocessMovingImage =
        PreprocessImage<ImageType>( movingImage,
                                    lowerScaleValue, upperScaleValue,
                                    this->m_LowerQuantile, this->m_UpperQuantile,
                                    NULL );
      }

    // Get the number of iterations and use that information to specify the number of levels

    std::vector<unsigned int> iterations = this->m_Iterations[currentStage];
    std::cout << "  iterations = " ;
    for(unsigned m = 0; m < iterations.size(); m++)
      {
      std::cout << iterations[m];
      if(m < iterations.size() - 1)
        {
        std::cout << 'x';
        }
      }
    std::cout << std::endl;
    unsigned int numberOfLevels = iterations.size();
    std::cout << "  number of levels = " << numberOfLevels << std::endl;

    // Get shrink factors

    std::vector<unsigned int> factors = this->m_ShrinkFactors[currentStage];
    typename AffineRegistrationType::ShrinkFactorsArrayType shrinkFactorsPerLevel;
    shrinkFactorsPerLevel.SetSize( factors.size() );

    if( factors.size() != numberOfLevels )
      {
      std::cerr << "ERROR:  The number of shrink factors does not match the number of levels." << std::endl;
      return EXIT_FAILURE;
      }
    else
      {
      for( unsigned int n = 0; n < shrinkFactorsPerLevel.Size(); n++ )
        {
        shrinkFactorsPerLevel[n] = factors[n];
        }
      std::cout << "  shrink factors per level: " << shrinkFactorsPerLevel << std::endl;
      }

    // Get smoothing sigmas

    std::vector<float> sigmas = this->m_SmoothingSigmas[currentStage];
    typename AffineRegistrationType::SmoothingSigmasArrayType smoothingSigmasPerLevel;
    smoothingSigmasPerLevel.SetSize( sigmas.size() );

    if( sigmas.size() != numberOfLevels )
      {
      std::cerr << "ERROR:  The number of smoothing sigmas does not match the number of levels." << std::endl;
      return EXIT_FAILURE;
      }
    else
      {
      for( unsigned int n = 0; n < smoothingSigmasPerLevel.Size(); n++ )
        {
        smoothingSigmasPerLevel[n] = sigmas[n];
        }
      std::cout << "  smoothing sigmas per level: " << smoothingSigmasPerLevel << std::endl;
      }

    // Set up the image metric and scales estimator

    typedef itk::ImageToImageMetricv4<ImageType, ImageType> MetricType;
    typename MetricType::Pointer metric;

    float samplingPercentage = this->m_Metrics[currentStage].m_SamplingPercentage;
    SamplingStrategy samplingStrategy = this->m_Metrics[currentStage].m_SamplingStrategy;
    typename AffineRegistrationType::MetricSamplingStrategyType metricSamplingStrategy = AffineRegistrationType::NONE;

    if( samplingStrategy == random)
      {
      std::cout << "  random sampling (percentage = " << samplingPercentage << ")" << std::endl;
      metricSamplingStrategy = AffineRegistrationType::RANDOM;
      }
    else if( samplingStrategy == regular)
      {
      std::cout << "  regular sampling (percentage = " << samplingPercentage << ")" << std::endl;
      metricSamplingStrategy = AffineRegistrationType::REGULAR;
      }

    switch(this->m_Metrics[currentStage].m_MetricType)
      {
      case CC:
      {
      unsigned int radiusOption = this->m_Metrics[currentStage].m_Radius;

      std::cout << "  using the CC metric (radius = " << radiusOption << ")" << std::endl;
      typedef itk::ANTSNeighborhoodCorrelationImageToImageMetricv4<ImageType, ImageType> CorrelationMetricType;
      typename CorrelationMetricType::Pointer correlationMetric = CorrelationMetricType::New();
      typename CorrelationMetricType::RadiusType radius;
      radius.Fill( radiusOption );
      correlationMetric->SetRadius( radius );
      correlationMetric->SetUseMovingImageGradientFilter( false );
      correlationMetric->SetUseFixedImageGradientFilter( false );

      metric = correlationMetric;
      }
      break;
      case MI:
      {
      unsigned int binOption = this->m_Metrics[currentStage].m_NumberOfBins;
      std::cout << "  using the Mattes MI metric (number of bins = " << binOption << ")" << std::endl;
      typedef itk::MattesMutualInformationImageToImageMetricv4<ImageType, ImageType> MutualInformationMetricType;
      typename MutualInformationMetricType::Pointer mutualInformationMetric = MutualInformationMetricType::New();
      mutualInformationMetric = mutualInformationMetric;
      mutualInformationMetric->SetNumberOfHistogramBins( binOption );
      mutualInformationMetric->SetUseMovingImageGradientFilter( false );
      mutualInformationMetric->SetUseFixedImageGradientFilter( false );
      mutualInformationMetric->SetUseFixedSampledPointSet( false );
      metric = mutualInformationMetric;
      }
      break;
#if 0 // apparently this isn't actually implemented?
      case MI2:
      {
      unsigned int binOption = parser->Convert<unsigned int>( metricOption->GetParameter( currentStage, 3 ) );

      std::cout << "  using the MI metric (number of bins = " << binOption << ")" << std::endl;
      typedef itk::JointHistogramMutualInformationImageToImageMetricv4<ImageType,
                                                                       ImageType> MutualInformationMetricType;
      typename MutualInformationMetricType::Pointer mutualInformationMetric = MutualInformationMetricType::New();
      mutualInformationMetric = mutualInformationMetric;
      mutualInformationMetric->SetNumberOfHistogramBins( binOption );
      mutualInformationMetric->SetUseMovingImageGradientFilter( false );
      mutualInformationMetric->SetUseFixedImageGradientFilter( false );
      mutualInformationMetric->SetUseFixedSampledPointSet( false );
      mutualInformationMetric->SetVarianceForJointPDFSmoothing( 1.0 );
      metric = mutualInformationMetric;
      }
      break;
#endif
      case MeanSquares:
      {
      std::cout << "  using the MeanSquares metric." << std::endl;

      typedef itk::MeanSquaresImageToImageMetricv4<ImageType, ImageType> MeanSquaresMetricType;
      typename MeanSquaresMetricType::Pointer meanSquaresMetric = MeanSquaresMetricType::New();
      meanSquaresMetric = meanSquaresMetric;

      metric = meanSquaresMetric;
      }
      break;
      case GC:
      {
      std::cout << "  using the global correlation metric." << std::endl;
      typedef itk::CorrelationImageToImageMetricv4<ImageType, ImageType> corrMetricType;
      typename corrMetricType::Pointer corrMetric = corrMetricType::New();
      metric = corrMetric;
      }
      break;
      default:
        std::cerr << "ERROR: Unrecognized image metric: " << std::endl;
      }
    /** Can really impact performance */
    bool gaussian = false;
    metric->SetUseMovingImageGradientFilter( gaussian );
    metric->SetUseFixedImageGradientFilter( gaussian );

    // Set up the optimizer.  To change the iteration number for each level we rely
    // on the command observer.

    float learningRate = this->m_TransformMethods[currentStage].m_GradientStep;
    typedef itk::RegistrationParameterScalesFromShift<MetricType> ScalesEstimatorType;
    typename ScalesEstimatorType::Pointer scalesEstimator = ScalesEstimatorType::New();
    scalesEstimator->SetMetric( metric );
    scalesEstimator->SetTransformForward( true );

    typedef itk::GradientDescentOptimizerv4 GradientDescentOptimizerType;
    typename GradientDescentOptimizerType::Pointer optimizer = GradientDescentOptimizerType::New();
    optimizer->SetLearningRate( learningRate );
    optimizer->SetMaximumStepSizeInPhysicalUnits( learningRate );
    optimizer->SetNumberOfIterations( iterations[0] );
    optimizer->SetScalesEstimator( scalesEstimator );
    //    optimizer->SetMinimumConvergenceValue( -1 );
    //    optimizer->SetConvergenceWindowSize( 10 );



    // Set up the image registration methods along with the transforms
    XfrmMethod whichTransform = this->m_TransformMethods[currentStage].m_XfrmMethod;
    switch(whichTransform)
      {
      case Affine:
      {
      typename AffineRegistrationType::Pointer affineRegistration = AffineRegistrationType::New();

      typedef itk::AffineTransform<double, VImageDimension> AffineTransformType;

      affineRegistration->SetFixedImage( preprocessFixedImage );
      affineRegistration->SetMovingImage( preprocessMovingImage );
      affineRegistration->SetNumberOfLevels( numberOfLevels );
      affineRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      affineRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      affineRegistration->SetMetric( metric );
      affineRegistration->SetMetricSamplingStrategy( metricSamplingStrategy );
      affineRegistration->SetMetricSamplingPercentage( samplingPercentage );
      affineRegistration->SetOptimizer( optimizer );
      affineRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<AffineRegistrationType> AffineCommandType;
      typename AffineCommandType::Pointer affineObserver = AffineCommandType::New();
      affineObserver->SetNumberOfIterations( iterations );

      affineRegistration->AddObserver( itk::IterationEvent(), affineObserver );

      try
        {
        std::cout << std::endl << "*** Running affine registration ***" << std::endl << std::endl;
        affineObserver->Execute( affineRegistration, itk::StartEvent() );
        affineRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }

      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( const_cast<AffineTransformType *>( affineRegistration->GetOutput()->Get() ) );

      // Write out the affine transform

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Affine.mat" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( affineRegistration->GetOutput()->Get() );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      break;
      case Rigid:
      {
      typedef typename RigidTransformTraits<VImageDimension>::TransformType RigidTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType, RigidTransformType> RigidRegistrationType;
      typename RigidRegistrationType::Pointer rigidRegistration = RigidRegistrationType::New();

      rigidRegistration->SetFixedImage( preprocessFixedImage );
      rigidRegistration->SetMovingImage( preprocessMovingImage );
      rigidRegistration->SetNumberOfLevels( numberOfLevels );
      rigidRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      rigidRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      rigidRegistration->SetMetric( metric );
      rigidRegistration->SetMetricSamplingStrategy(
        static_cast<typename RigidRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      rigidRegistration->SetMetricSamplingPercentage( samplingPercentage );
      rigidRegistration->SetOptimizer( optimizer );
      rigidRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<RigidRegistrationType> RigidCommandType;
      typename RigidCommandType::Pointer rigidObserver = RigidCommandType::New();
      rigidObserver->SetNumberOfIterations( iterations );

      rigidRegistration->AddObserver( itk::IterationEvent(), rigidObserver );

      try
        {
        std::cout << std::endl << "*** Running rigid registration ***" << std::endl << std::endl;
        rigidObserver->Execute( rigidRegistration, itk::StartEvent() );
        rigidRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( const_cast<RigidTransformType *>( rigidRegistration->GetOutput()->Get() ) );

      // Write out the rigid transform
      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Rigid.mat" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( rigidRegistration->GetOutput()->Get() );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      break;
      case CompositeAffine:
      {
      typedef typename CompositeAffineTransformTraits<VImageDimension>::TransformType CompositeAffineTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType, CompositeAffineTransformType> AffineRegistrationType;
      typename AffineRegistrationType::Pointer affineRegistration = AffineRegistrationType::New();

      affineRegistration->SetFixedImage( preprocessFixedImage );
      affineRegistration->SetMovingImage( preprocessMovingImage );
      affineRegistration->SetNumberOfLevels( numberOfLevels );
      affineRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      affineRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      affineRegistration->SetMetric( metric );
      affineRegistration->SetMetricSamplingStrategy(
        static_cast<typename AffineRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      affineRegistration->SetMetricSamplingPercentage( samplingPercentage );
      affineRegistration->SetOptimizer( optimizer );
      affineRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<AffineRegistrationType> AffineCommandType;
      typename AffineCommandType::Pointer affineObserver = AffineCommandType::New();
      affineObserver->SetNumberOfIterations( iterations );

      affineRegistration->AddObserver( itk::IterationEvent(), affineObserver );

      try
        {
        std::cout << std::endl << "*** Running composite affine registration ***" << std::endl << std::endl;
        affineObserver->Execute( affineRegistration, itk::StartEvent() );
        affineRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( const_cast<CompositeAffineTransformType *>( affineRegistration->GetOutput()->
                                                                                    Get() ) );

      // Write out the affine transform

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Affine.mat" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( affineRegistration->GetOutput()->Get() );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      break;
      case Similarity:
      {
      typedef typename SimilarityTransformTraits<VImageDimension>::TransformType SimilarityTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType, SimilarityTransformType> SimilarityRegistrationType;
      typename SimilarityRegistrationType::Pointer similarityRegistration = SimilarityRegistrationType::New();

      similarityRegistration->SetFixedImage( preprocessFixedImage );
      similarityRegistration->SetMovingImage( preprocessMovingImage );
      similarityRegistration->SetNumberOfLevels( numberOfLevels );
      similarityRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      similarityRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      similarityRegistration->SetMetric( metric );
      similarityRegistration->SetMetricSamplingStrategy(
        static_cast<typename SimilarityRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      similarityRegistration->SetMetricSamplingPercentage( samplingPercentage );
      similarityRegistration->SetOptimizer( optimizer );
      similarityRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<SimilarityRegistrationType> SimilarityCommandType;
      typename SimilarityCommandType::Pointer similarityObserver = SimilarityCommandType::New();
      similarityObserver->SetNumberOfIterations( iterations );

      similarityRegistration->AddObserver( itk::IterationEvent(), similarityObserver );

      try
        {
        std::cout << std::endl << "*** Running similarity registration ***" << std::endl << std::endl;
        similarityObserver->Execute( similarityRegistration, itk::StartEvent() );
        similarityRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( const_cast<SimilarityTransformType *>( similarityRegistration->GetOutput()->Get() ) );

      // Write out the similarity transform

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Similarity.mat" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( similarityRegistration->GetOutput()->Get() );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      break;
      case Translation:
      {
      typedef itk::TranslationTransform<RealType, VImageDimension> TranslationTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType,
                                             TranslationTransformType> TranslationRegistrationType;
      typename TranslationRegistrationType::Pointer translationRegistration = TranslationRegistrationType::New();

      translationRegistration->SetFixedImage( preprocessFixedImage );
      translationRegistration->SetMovingImage( preprocessMovingImage );
      translationRegistration->SetNumberOfLevels( numberOfLevels );
      translationRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      translationRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      translationRegistration->SetMetric( metric );
      translationRegistration->SetMetricSamplingStrategy(
        static_cast<typename TranslationRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      translationRegistration->SetMetricSamplingPercentage( samplingPercentage );
      translationRegistration->SetOptimizer( optimizer );
      translationRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<TranslationRegistrationType> TranslationCommandType;
      typename TranslationCommandType::Pointer translationObserver = TranslationCommandType::New();
      translationObserver->SetNumberOfIterations( iterations );

      translationRegistration->AddObserver( itk::IterationEvent(), translationObserver );

      try
        {
        std::cout << std::endl << "*** Running translation registration ***" << std::endl << std::endl;
        translationObserver->Execute( translationRegistration, itk::StartEvent() );
        translationRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( const_cast<TranslationTransformType *>( translationRegistration->GetOutput()->
                                                                                Get() ) );

      // Write out the translation transform

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Translation.mat" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( translationRegistration->GetOutput()->Get() );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      case GaussianDisplacementField:
      {
      typedef itk::Vector<RealType, VImageDimension> VectorType;
      VectorType zeroVector( 0.0 );
      typedef itk::Image<VectorType, VImageDimension> DisplacementFieldType;
      typename DisplacementFieldType::Pointer displacementField = DisplacementFieldType::New();
      displacementField->CopyInformation( fixedImage );
      displacementField->SetRegions( fixedImage->GetBufferedRegion() );
      displacementField->Allocate();
      displacementField->FillBuffer( zeroVector );

      typedef GaussianSmoothingOnUpdateDisplacementFieldTransform<RealType,
                                                                  VImageDimension> DisplacementFieldTransformType;

      typedef ImageRegistrationMethodv4<ImageType, ImageType,
                                             DisplacementFieldTransformType> DisplacementFieldRegistrationType;
      typename DisplacementFieldRegistrationType::Pointer displacementFieldRegistration =
        DisplacementFieldRegistrationType::New();

      typename DisplacementFieldTransformType::Pointer outputDisplacementFieldTransform =
        const_cast<DisplacementFieldTransformType *>( displacementFieldRegistration->GetOutput()->Get() );

      // Create the transform adaptors

      typedef itk::DisplacementFieldTransformParametersAdaptor<DisplacementFieldTransformType>
      DisplacementFieldTransformAdaptorType;
      typename DisplacementFieldRegistrationType::TransformParametersAdaptorsContainerType adaptors;

      // Extract parameters

      RealType varianceForUpdateField = this->m_TransformMethods[currentStage].m_UpdateFieldSigmaInPhysicalSpace;
      RealType varianceForTotalField  = this->m_TransformMethods[currentStage].m_TotalFieldSigmaInPhysicalSpace;

      outputDisplacementFieldTransform->SetGaussianSmoothingVarianceForTheUpdateField( varianceForUpdateField );
      outputDisplacementFieldTransform->SetGaussianSmoothingVarianceForTheTotalField( varianceForTotalField );
      outputDisplacementFieldTransform->SetDisplacementField( displacementField );
      // Create the transform adaptors
      // For the gaussian displacement field, the specified variances are in image spacing terms
      // and, in normal practice, we typically don't change these values at each level.  However,
      // if the user wishes to add that option, they can use the class
      // GaussianSmoothingOnUpdateDisplacementFieldTransformAdaptor
      for( unsigned int level = 0; level < numberOfLevels; level++ )
        {
        // We use the shrink image filter to calculate the fixed parameters of the virtual
        // domain at each level.  To speed up calculation and avoid unnecessary memory
        // usage, we could calculate these fixed parameters directly.

        typedef itk::ShrinkImageFilter<DisplacementFieldType, DisplacementFieldType> ShrinkFilterType;
        typename ShrinkFilterType::Pointer shrinkFilter = ShrinkFilterType::New();
        shrinkFilter->SetShrinkFactors( shrinkFactorsPerLevel[level] );
        shrinkFilter->SetInput( displacementField );
        shrinkFilter->Update();

        typename DisplacementFieldTransformAdaptorType::Pointer fieldTransformAdaptor =
          DisplacementFieldTransformAdaptorType::New();
        fieldTransformAdaptor->SetRequiredSpacing( shrinkFilter->GetOutput()->GetSpacing() );
        fieldTransformAdaptor->SetRequiredSize( shrinkFilter->GetOutput()->GetBufferedRegion().GetSize() );
        fieldTransformAdaptor->SetRequiredDirection( shrinkFilter->GetOutput()->GetDirection() );
        fieldTransformAdaptor->SetRequiredOrigin( shrinkFilter->GetOutput()->GetOrigin() );
        fieldTransformAdaptor->SetTransform( outputDisplacementFieldTransform );

        adaptors.push_back( fieldTransformAdaptor.GetPointer() );
        }

      displacementFieldRegistration->SetFixedImage( preprocessFixedImage );
      displacementFieldRegistration->SetMovingImage( preprocessMovingImage );
      displacementFieldRegistration->SetNumberOfLevels( numberOfLevels );
      displacementFieldRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      displacementFieldRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      displacementFieldRegistration->SetMetric( metric );
      displacementFieldRegistration->SetMetricSamplingStrategy(
        static_cast<typename DisplacementFieldRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      displacementFieldRegistration->SetMetricSamplingPercentage( samplingPercentage );
      displacementFieldRegistration->SetOptimizer( optimizer );
      displacementFieldRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );
      displacementFieldRegistration->SetMovingInitialTransform( compositeTransform );

      typedef CommandIterationUpdate<DisplacementFieldRegistrationType> DisplacementFieldCommandType;
      typename DisplacementFieldCommandType::Pointer displacementFieldRegistrationObserver =
        DisplacementFieldCommandType::New();
      displacementFieldRegistrationObserver->SetNumberOfIterations( iterations );

      displacementFieldRegistration->AddObserver( itk::IterationEvent(), displacementFieldRegistrationObserver );

      try
        {
        std::cout << std::endl << "*** Running gaussian displacement field registration (varianceForUpdateField = "
                  << varianceForUpdateField << ", varianceForTotalField = " << varianceForTotalField << ") ***"
                  << std::endl << std::endl;
        displacementFieldRegistrationObserver->Execute( displacementFieldRegistration, itk::StartEvent() );
        displacementFieldRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }

      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputDisplacementFieldTransform );

      // Write out the displacement field

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Warp.nii.gz" );

      typedef itk::ImageFileWriter<DisplacementFieldType> WriterType;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput( outputDisplacementFieldTransform->GetDisplacementField() );
      writer->SetFileName( filename.c_str() );
      writer->Update();
      }
      case BSplineDisplacementField:
      {
      typedef itk::Vector<RealType, VImageDimension> VectorType;
      VectorType zeroVector( 0.0 );
      typedef itk::Image<VectorType, VImageDimension> DisplacementFieldType;
      typename DisplacementFieldType::Pointer displacementField = DisplacementFieldType::New();
      displacementField->CopyInformation( fixedImage );
      displacementField->SetRegions( fixedImage->GetBufferedRegion() );
      displacementField->Allocate();
      displacementField->FillBuffer( zeroVector );

      typedef itk::BSplineSmoothingOnUpdateDisplacementFieldTransform<RealType,
                                                                      VImageDimension> DisplacementFieldTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType,
                                             DisplacementFieldTransformType> DisplacementFieldRegistrationType;
      typename DisplacementFieldRegistrationType::Pointer displacementFieldRegistration =
        DisplacementFieldRegistrationType::New();

      typename DisplacementFieldTransformType::Pointer outputDisplacementFieldTransform =
        const_cast<DisplacementFieldTransformType *>( displacementFieldRegistration->GetOutput()->Get() );
      outputDisplacementFieldTransform->SetDisplacementField( displacementField );

      // Create the transform adaptors

      typedef itk::DisplacementFieldTransformParametersAdaptor<DisplacementFieldTransformType>
      DisplacementFieldTransformAdaptorType;
      typename DisplacementFieldRegistrationType::TransformParametersAdaptorsContainerType adaptors;

      // Extract parameters

      const std::vector<unsigned int> &meshSizeForTheUpdateField =
        this->m_TransformMethods[currentStage].m_UpdateFieldMeshSizeAtBaseLevel;
      std::vector<unsigned int> meshSizeForTheTotalField =
        this->m_TransformMethods[currentStage].m_TotalFieldMeshSizeAtBaseLevel;

      if( meshSizeForTheUpdateField.size() != VImageDimension || meshSizeForTheTotalField.size() != VImageDimension )
        {
        std::cerr << "ERROR:  The mesh size(s) don't match the ImageDimension." << std::endl;
        return EXIT_FAILURE;
        }

      typename DisplacementFieldTransformType::ArrayType updateMeshSize;
      typename DisplacementFieldTransformType::ArrayType totalMeshSize;
      for( unsigned int d = 0; d < VImageDimension; d++ )
        {
        updateMeshSize[d] = meshSizeForTheUpdateField[d];
        totalMeshSize[d] = meshSizeForTheTotalField[d];
        }
      // Create the transform adaptors specific to B-splines
      for( unsigned int level = 0; level < numberOfLevels; level++ )
        {
        // We use the shrink image filter to calculate the fixed parameters of the virtual
        // domain at each level.  To speed up calculation and avoid unnecessary memory
        // usage, we could calculate these fixed parameters directly.

        typedef itk::ShrinkImageFilter<DisplacementFieldType, DisplacementFieldType> ShrinkFilterType;
        typename ShrinkFilterType::Pointer shrinkFilter = ShrinkFilterType::New();
        shrinkFilter->SetShrinkFactors( shrinkFactorsPerLevel[level] );
        shrinkFilter->SetInput( displacementField );
        shrinkFilter->Update();

        typedef itk::BSplineSmoothingOnUpdateDisplacementFieldTransformParametersAdaptor<DisplacementFieldTransformType>
        BSplineDisplacementFieldTransformAdaptorType;
        typename BSplineDisplacementFieldTransformAdaptorType::Pointer bsplineFieldTransformAdaptor =
          BSplineDisplacementFieldTransformAdaptorType::New();
        bsplineFieldTransformAdaptor->SetRequiredSpacing( shrinkFilter->GetOutput()->GetSpacing() );
        bsplineFieldTransformAdaptor->SetRequiredSize( shrinkFilter->GetOutput()->GetBufferedRegion().GetSize() );
        bsplineFieldTransformAdaptor->SetRequiredDirection( shrinkFilter->GetOutput()->GetDirection() );
        bsplineFieldTransformAdaptor->SetRequiredOrigin( shrinkFilter->GetOutput()->GetOrigin() );
        bsplineFieldTransformAdaptor->SetTransform( outputDisplacementFieldTransform );

        // A good heuristic is to double the b-spline mesh resolution at each level
        typename DisplacementFieldTransformType::ArrayType newUpdateMeshSize = updateMeshSize;
        typename DisplacementFieldTransformType::ArrayType newTotalMeshSize = totalMeshSize;
        for( unsigned int d = 0; d < VImageDimension; d++ )
          {
          newUpdateMeshSize[d] = newUpdateMeshSize[d] << ( level + 1 );
          newTotalMeshSize[d] = newTotalMeshSize[d] << ( level + 1 );
          }
        bsplineFieldTransformAdaptor->SetMeshSizeForTheUpdateField( newUpdateMeshSize );
        bsplineFieldTransformAdaptor->SetMeshSizeForTheTotalField( newTotalMeshSize );

        adaptors.push_back( bsplineFieldTransformAdaptor.GetPointer() );
        }

      displacementFieldRegistration->SetFixedImage( preprocessFixedImage );
      displacementFieldRegistration->SetMovingImage( preprocessMovingImage );
      displacementFieldRegistration->SetNumberOfLevels( numberOfLevels );
      displacementFieldRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      displacementFieldRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      displacementFieldRegistration->SetMovingInitialTransform( compositeTransform );
      displacementFieldRegistration->SetMetric( metric );
      displacementFieldRegistration->SetMetricSamplingStrategy(
        static_cast<typename DisplacementFieldRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      displacementFieldRegistration->SetMetricSamplingPercentage( samplingPercentage );
      displacementFieldRegistration->SetOptimizer( optimizer );
      displacementFieldRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );

      typedef CommandIterationUpdate<DisplacementFieldRegistrationType> DisplacementFieldCommandType;
      typename DisplacementFieldCommandType::Pointer displacementFieldRegistrationObserver =
        DisplacementFieldCommandType::New();
      displacementFieldRegistrationObserver->SetNumberOfIterations( iterations );

      displacementFieldRegistration->AddObserver( itk::IterationEvent(), displacementFieldRegistrationObserver );

      try
        {
        std::cout << std::endl << "*** Running bspline displacement field registration (updateMeshSizeAtBaseLevel = "
                  << updateMeshSize << ", totalMeshSizeAtBaseLevel = " << totalMeshSize << ") ***" << std::endl
                  << std::endl;
        displacementFieldRegistrationObserver->Execute( displacementFieldRegistration, itk::StartEvent() );
        displacementFieldRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }

      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputDisplacementFieldTransform );

      // Write out the displacement field

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Warp.nii.gz" );

      typedef itk::ImageFileWriter<DisplacementFieldType> WriterType;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput( outputDisplacementFieldTransform->GetDisplacementField() );
      writer->SetFileName( filename.c_str() );
      writer->Update();
      }
      break;
      case BSpline:
      {
      const unsigned int SplineOrder = 3;
      typedef itk::BSplineTransform<RealType, VImageDimension, SplineOrder> BSplineTransformType;

      typedef itk::ImageRegistrationMethodv4<ImageType, ImageType, BSplineTransformType> BSplineRegistrationType;
      typename BSplineRegistrationType::Pointer bsplineRegistration = BSplineRegistrationType::New();

      typename BSplineTransformType::Pointer outputBSplineTransform =
        const_cast<BSplineTransformType *>( bsplineRegistration->GetOutput()->Get() );

      const std::vector<unsigned int> &size =
        this->m_TransformMethods[currentStage].m_MeshSizeAtBaseLevel;

      typename BSplineTransformType::PhysicalDimensionsType physicalDimensions;
      typename BSplineTransformType::MeshSizeType meshSize;
      for( unsigned int d = 0; d < VImageDimension; d++ )
        {
        physicalDimensions[d] = fixedImage->GetSpacing()[d]
          * static_cast<RealType>( fixedImage->GetLargestPossibleRegion().GetSize()[d] - 1 );
        meshSize[d] = size[d];
        }

      // Create the transform adaptors

      typedef itk::BSplineTransformParametersAdaptor<BSplineTransformType> BSplineTransformAdaptorType;
      typename BSplineRegistrationType::TransformParametersAdaptorsContainerType adaptors;
      // Create the transform adaptors specific to B-splines
      for( unsigned int level = 0; level < numberOfLevels; level++ )
        {
        // A good heuristic is to double the b-spline mesh resolution at each level

        typename BSplineTransformType::MeshSizeType requiredMeshSize;
        for( unsigned int d = 0; d < VImageDimension; d++ )
          {
          requiredMeshSize[d] = meshSize[d] << level;
          }

        typedef itk::BSplineTransformParametersAdaptor<BSplineTransformType> BSplineAdaptorType;
        typename BSplineAdaptorType::Pointer bsplineAdaptor = BSplineAdaptorType::New();
        bsplineAdaptor->SetTransform( outputBSplineTransform );
        bsplineAdaptor->SetRequiredTransformDomainMeshSize( requiredMeshSize );
        bsplineAdaptor->SetRequiredTransformDomainOrigin( outputBSplineTransform->GetTransformDomainOrigin() );
        bsplineAdaptor->SetRequiredTransformDomainDirection( outputBSplineTransform->GetTransformDomainDirection() );
        bsplineAdaptor->SetRequiredTransformDomainPhysicalDimensions(
          outputBSplineTransform->GetTransformDomainPhysicalDimensions() );

        adaptors.push_back( bsplineAdaptor.GetPointer() );
        }

      optimizer->SetScalesEstimator( NULL );

      bsplineRegistration->SetFixedImage( preprocessFixedImage );
      bsplineRegistration->SetMovingImage( preprocessMovingImage );
      bsplineRegistration->SetNumberOfLevels( numberOfLevels );
      bsplineRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      bsplineRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      bsplineRegistration->SetMetric( metric );
      bsplineRegistration->SetMetricSamplingStrategy(
        static_cast<typename BSplineRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      bsplineRegistration->SetMetricSamplingPercentage( samplingPercentage );
      bsplineRegistration->SetOptimizer( optimizer );
      bsplineRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );
      outputBSplineTransform->SetTransformDomainOrigin( fixedImage->GetOrigin() );
      outputBSplineTransform->SetTransformDomainPhysicalDimensions( physicalDimensions );
      outputBSplineTransform->SetTransformDomainMeshSize( meshSize );
      outputBSplineTransform->SetTransformDomainDirection( fixedImage->GetDirection() );
      outputBSplineTransform->SetIdentity();

      typedef CommandIterationUpdate<BSplineRegistrationType> BSplineCommandType;
      typename BSplineCommandType::Pointer bsplineObserver = BSplineCommandType::New();
      bsplineObserver->SetNumberOfIterations( iterations );

      bsplineRegistration->AddObserver( itk::IterationEvent(), bsplineObserver );

      try
        {
        std::cout << std::endl << "*** Running bspline registration (meshSizeAtBaseLevel = " << meshSize << ") ***"
                  << std::endl << std::endl;
        bsplineObserver->Execute( bsplineRegistration, itk::StartEvent() );
        bsplineRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputBSplineTransform );

      // Write out B-spline transform

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "BSpline.txt" );

      typedef itk::TransformFileWriter TransformWriterType;
      typename TransformWriterType::Pointer transformWriter = TransformWriterType::New();
      transformWriter->SetInput( outputBSplineTransform );
      transformWriter->SetFileName( filename.c_str() );
      transformWriter->Update();
      }
      break;
      case TimeVaryingVelocityField:
      {
      typedef itk::Vector<RealType, VImageDimension> VectorType;
      VectorType zeroVector( 0.0 );

      // Determine the parameters (size, spacing, etc) for the time-varying velocity field

      typedef itk::Image<VectorType, VImageDimension + 1> TimeVaryingVelocityFieldType;
      typename TimeVaryingVelocityFieldType::Pointer velocityField = TimeVaryingVelocityFieldType::New();

      typename TimeVaryingVelocityFieldType::IndexType velocityFieldIndex;
      typename TimeVaryingVelocityFieldType::SizeType velocityFieldSize;
      typename TimeVaryingVelocityFieldType::PointType velocityFieldOrigin;
      typename TimeVaryingVelocityFieldType::SpacingType velocityFieldSpacing;
      typename TimeVaryingVelocityFieldType::DirectionType velocityFieldDirection;
      typename TimeVaryingVelocityFieldType::RegionType velocityFieldRegion;

      typename ImageType::IndexType fixedImageIndex = fixedImage->GetBufferedRegion().GetIndex();
      typename ImageType::SizeType fixedImageSize = fixedImage->GetBufferedRegion().GetSize();
      typename ImageType::PointType fixedImageOrigin = fixedImage->GetOrigin();
      typename ImageType::SpacingType fixedImageSpacing = fixedImage->GetSpacing();
      typename ImageType::DirectionType fixedImageDirection = fixedImage->GetDirection();

      unsigned int numberOfTimeIndices = this->m_TransformMethods[currentStage].m_NumberOfTimeIndices;

      velocityFieldIndex.Fill( 0 );
      velocityFieldSize.Fill( numberOfTimeIndices );
      velocityFieldOrigin.Fill( 0.0 );
      velocityFieldSpacing.Fill( 1.0 );
      velocityFieldDirection.SetIdentity();
      for( unsigned int i = 0; i < VImageDimension; i++ )
        {
        velocityFieldIndex[i] = fixedImageIndex[i];
        velocityFieldSize[i] = fixedImageSize[i];
        velocityFieldOrigin[i] = fixedImageOrigin[i];
        velocityFieldSpacing[i] = fixedImageSpacing[i];
        for( unsigned int j = 0; j < VImageDimension; j++ )
          {
          velocityFieldDirection[i][j] = fixedImageDirection[i][j];
          }
        }

      velocityFieldRegion.SetSize( velocityFieldSize );
      velocityFieldRegion.SetIndex( velocityFieldIndex );

      velocityField->SetOrigin( velocityFieldOrigin );
      velocityField->SetSpacing( velocityFieldSpacing );
      velocityField->SetDirection( velocityFieldDirection );
      velocityField->SetRegions( velocityFieldRegion );
      velocityField->Allocate();
      velocityField->FillBuffer( zeroVector );

      // Extract parameters

      RealType varianceForUpdateField = this->m_TransformMethods[currentStage].m_UpdateFieldSigmaInPhysicalSpace;
      RealType varianceForUpdateFieldTime = this->m_TransformMethods[currentStage].m_UpdateFieldTimeSigma;
      RealType varianceForTotalField = this->m_TransformMethods[currentStage].m_TotalFieldSigmaInPhysicalSpace;
      RealType varianceForTotalFieldTime = this->m_TransformMethods[currentStage].m_TotalFieldTimeSigma;

      typedef itk::TimeVaryingVelocityFieldImageRegistrationMethodv4<ImageType,
                                                                     ImageType> VelocityFieldRegistrationType;
      typename VelocityFieldRegistrationType::Pointer velocityFieldRegistration = VelocityFieldRegistrationType::New();

      typedef typename VelocityFieldRegistrationType::OutputTransformType OutputTransformType;
      typename OutputTransformType::Pointer outputTransform =
        const_cast<OutputTransformType *>( velocityFieldRegistration->GetOutput()->Get() );

      velocityFieldRegistration->SetFixedImage( preprocessFixedImage );
      velocityFieldRegistration->SetMovingImage( preprocessMovingImage );
      velocityFieldRegistration->SetMovingInitialTransform( compositeTransform );
      velocityFieldRegistration->SetNumberOfLevels( numberOfLevels );
      velocityFieldRegistration->SetMetric( metric );
      velocityFieldRegistration->SetMetricSamplingStrategy(
        static_cast<typename VelocityFieldRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      velocityFieldRegistration->SetMetricSamplingPercentage( samplingPercentage );
      velocityFieldRegistration->SetLearningRate( learningRate );
      outputTransform->SetGaussianSpatialSmoothingVarianceForTheTotalField( varianceForTotalField );
      outputTransform->SetGaussianSpatialSmoothingVarianceForTheUpdateField( varianceForUpdateField );
      outputTransform->SetGaussianTemporalSmoothingVarianceForTheTotalField( varianceForTotalFieldTime );
      outputTransform->SetGaussianTemporalSmoothingVarianceForTheUpdateField( varianceForUpdateFieldTime );

      outputTransform->SetTimeVaryingVelocityField( velocityField );
      outputTransform->SetLowerTimeBound( 0.0 );
      outputTransform->SetUpperTimeBound( 1.0 );

      typename VelocityFieldRegistrationType::NumberOfIterationsArrayType numberOfIterationsPerLevel;
      numberOfIterationsPerLevel.SetSize( numberOfLevels );
      for( unsigned int d = 0; d < numberOfLevels; d++ )
        {
        numberOfIterationsPerLevel[d] = iterations[d];
        }
      velocityFieldRegistration->SetNumberOfIterationsPerLevel( numberOfIterationsPerLevel );
      velocityFieldRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      velocityFieldRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );

      typedef itk::TimeVaryingVelocityFieldTransformParametersAdaptor<OutputTransformType>
      VelocityFieldTransformAdaptorType;

      typename VelocityFieldRegistrationType::TransformParametersAdaptorsContainerType adaptors;
      for( unsigned int level = 0; level < shrinkFactorsPerLevel.Size(); level++ )
        {
        typedef itk::ShrinkImageFilter<ImageType, ImageType> ShrinkFilterType;
        typename ShrinkFilterType::Pointer shrinkFilter = ShrinkFilterType::New();
        shrinkFilter->SetShrinkFactors( shrinkFactorsPerLevel[level] );
        shrinkFilter->SetInput( fixedImage );
        shrinkFilter->Update();

        // Although we shrink the images for the given levels,
        // we keep the size in time the same

        velocityFieldSize.Fill( numberOfTimeIndices );
        velocityFieldOrigin.Fill( 0.0 );
        velocityFieldSpacing.Fill( 1.0 );
        velocityFieldDirection.SetIdentity();

        fixedImageSize = shrinkFilter->GetOutput()->GetBufferedRegion().GetSize();
        fixedImageOrigin = shrinkFilter->GetOutput()->GetOrigin();
        fixedImageSpacing = shrinkFilter->GetOutput()->GetSpacing();
        fixedImageDirection = shrinkFilter->GetOutput()->GetDirection();
        for( unsigned int i = 0; i < VImageDimension; i++ )
          {
          velocityFieldSize[i] = fixedImageSize[i];
          velocityFieldOrigin[i] = fixedImageOrigin[i];
          velocityFieldSpacing[i] = fixedImageSpacing[i];
          for( unsigned int j = 0; j < VImageDimension; j++ )
            {
            velocityFieldDirection[i][j] = fixedImageDirection[i][j];
            }
          }

        typename VelocityFieldTransformAdaptorType::Pointer fieldTransformAdaptor =
          VelocityFieldTransformAdaptorType::New();
        fieldTransformAdaptor->SetRequiredSpacing( velocityFieldSpacing );
        fieldTransformAdaptor->SetRequiredSize( velocityFieldSize );
        fieldTransformAdaptor->SetRequiredDirection( velocityFieldDirection );
        fieldTransformAdaptor->SetRequiredOrigin( velocityFieldOrigin );

        adaptors.push_back( fieldTransformAdaptor.GetPointer() );
        }

      velocityFieldRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );

      typedef CommandIterationUpdate<VelocityFieldRegistrationType> VelocityFieldCommandType;
      typename VelocityFieldCommandType::Pointer velocityFieldRegistrationObserver = VelocityFieldCommandType::New();
      velocityFieldRegistrationObserver->SetNumberOfIterations( iterations );

      velocityFieldRegistration->AddObserver( itk::IterationEvent(), velocityFieldRegistrationObserver );

      try
        {
        std::cout << std::endl << "*** Running time-varying velocity field registration (varianceForUpdateField = "
                  << varianceForUpdateField << ", varianceForTotalField = " << varianceForTotalField
                  << ", varianceForUpdateFieldTime = "
                  << varianceForUpdateFieldTime << ", varianceForTotalFieldTime = " << varianceForTotalFieldTime
                  << ") ***" << std::endl << std::endl;
        velocityFieldRegistrationObserver->Execute( velocityFieldRegistration, itk::StartEvent() );
        velocityFieldRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputTransform );

      // Write out the displacement fields

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Warp.nii.gz" );

      typedef typename OutputTransformType::DisplacementFieldType DisplacementFieldType;

      typedef itk::ImageFileWriter<DisplacementFieldType> WriterType;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput( outputTransform->GetDisplacementField() );
      writer->SetFileName( filename.c_str() );
      writer->Update();

      std::string inverseFilename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "InverseWarp.nii.gz" );

      typedef itk::ImageFileWriter<DisplacementFieldType> InverseWriterType;
      typename InverseWriterType::Pointer inverseWriter = InverseWriterType::New();
      inverseWriter->SetInput( outputTransform->GetInverseDisplacementField() );
      inverseWriter->SetFileName( inverseFilename.c_str() );
      inverseWriter->Update();
      }
      break;
      case TimeVaryingBSplineVelocityField:
      {
      typedef itk::Vector<RealType, VImageDimension> VectorType;
      VectorType zeroVector( 0.0 );

      // Determine the parameters (size, spacing, etc) for the time-varying velocity field control point lattice

      const std::vector<unsigned int> &meshSize = this->m_TransformMethods[currentStage].m_VelocityFieldMeshSize;
      if( meshSize.size() != VImageDimension + 1 )
        {
        std::cerr << "The transform domain mesh size does not have the correct number of elements."
                  << "For image dimension = " << VImageDimension << ", you need " << VImageDimension + 1
                  << "elements. " << std::endl;
        return EXIT_FAILURE;
        }

      unsigned int numberOfTimePointSamples =  this->m_TransformMethods[currentStage].m_NumberOfTimePointSamples;
      unsigned int splineOrder = this->m_TransformMethods[currentStage].m_SplineOrder;

      typedef itk::Image<VectorType, VImageDimension + 1> TimeVaryingVelocityFieldControlPointLatticeType;
      typename TimeVaryingVelocityFieldControlPointLatticeType::Pointer velocityFieldLattice =
        TimeVaryingVelocityFieldControlPointLatticeType::New();

      typename ImageType::SizeType fixedImageSize = fixedImage->GetBufferedRegion().GetSize();
      typename ImageType::PointType fixedImageOrigin = fixedImage->GetOrigin();
      typename ImageType::SpacingType fixedImageSpacing = fixedImage->GetSpacing();
      typename ImageType::DirectionType fixedImageDirection = fixedImage->GetDirection();

      typename TimeVaryingVelocityFieldControlPointLatticeType::SizeType transformDomainMeshSize;
      typename TimeVaryingVelocityFieldControlPointLatticeType::PointType transformDomainOrigin;
      typename TimeVaryingVelocityFieldControlPointLatticeType::SpacingType transformDomainPhysicalDimensions;
      typename TimeVaryingVelocityFieldControlPointLatticeType::DirectionType transformDomainDirection;

      transformDomainDirection.SetIdentity();
      transformDomainOrigin.Fill( 0.0 );
      transformDomainPhysicalDimensions.Fill( 1.0 );
      for( unsigned int i = 0; i < VImageDimension; i++ )
        {
        transformDomainOrigin[i] = fixedImageOrigin[i];
        transformDomainMeshSize[i] = 3;
        transformDomainPhysicalDimensions[i] = static_cast<double>( fixedImageSize[i] - 1 ) * fixedImageSpacing[i];
        for( unsigned int j = 0; j < VImageDimension; j++ )
          {
          transformDomainDirection[i][j] = fixedImageDirection[i][j];
          }
        }
      for( unsigned int i = 0; i < meshSize.size(); i++ )
        {
        transformDomainMeshSize[i] = meshSize[i];
        }
      typename TimeVaryingVelocityFieldControlPointLatticeType::SizeType initialTransformDomainMeshSize =
        transformDomainMeshSize;

      typedef itk::TimeVaryingBSplineVelocityFieldImageRegistrationMethod<ImageType,
                                                                          ImageType> VelocityFieldRegistrationType;
      typename VelocityFieldRegistrationType::Pointer velocityFieldRegistration = VelocityFieldRegistrationType::New();

      typedef typename VelocityFieldRegistrationType::OutputTransformType OutputTransformType;
      typename OutputTransformType::Pointer outputTransform =
        const_cast<OutputTransformType *>( velocityFieldRegistration->GetOutput()->Get() );

      velocityFieldRegistration->SetFixedImage( preprocessFixedImage );
      velocityFieldRegistration->SetMovingImage( preprocessMovingImage );
      velocityFieldRegistration->SetMovingInitialTransform( compositeTransform );
      velocityFieldRegistration->SetNumberOfLevels( numberOfLevels );
      velocityFieldRegistration->SetNumberOfTimePointSamples( numberOfTimePointSamples );
      velocityFieldRegistration->SetMetric( metric );
      velocityFieldRegistration->SetMetricSamplingStrategy(
        static_cast<typename VelocityFieldRegistrationType::MetricSamplingStrategyType>( metricSamplingStrategy ) );
      velocityFieldRegistration->SetMetricSamplingPercentage( samplingPercentage );
      velocityFieldRegistration->SetLearningRate( learningRate );
      outputTransform->SetSplineOrder( splineOrder );
      outputTransform->SetLowerTimeBound( 0.0 );
      outputTransform->SetUpperTimeBound( 1.0 );

      typedef itk::TimeVaryingBSplineVelocityFieldTransformParametersAdaptor<OutputTransformType>
      VelocityFieldTransformAdaptorType;
      typename VelocityFieldTransformAdaptorType::Pointer initialFieldTransformAdaptor =
        VelocityFieldTransformAdaptorType::New();
      initialFieldTransformAdaptor->SetTransform( outputTransform );
      initialFieldTransformAdaptor->SetRequiredTransformDomainOrigin( transformDomainOrigin );
      initialFieldTransformAdaptor->SetRequiredTransformDomainPhysicalDimensions( transformDomainPhysicalDimensions );
      initialFieldTransformAdaptor->SetRequiredTransformDomainMeshSize( transformDomainMeshSize );
      initialFieldTransformAdaptor->SetRequiredTransformDomainDirection( transformDomainDirection );

      velocityFieldLattice->SetOrigin( initialFieldTransformAdaptor->GetRequiredControlPointLatticeOrigin() );
      velocityFieldLattice->SetSpacing( initialFieldTransformAdaptor->GetRequiredControlPointLatticeSpacing() );
      velocityFieldLattice->SetDirection( initialFieldTransformAdaptor->GetRequiredControlPointLatticeDirection() );
      velocityFieldLattice->SetRegions( initialFieldTransformAdaptor->GetRequiredControlPointLatticeSize() );
      velocityFieldLattice->Allocate();
      velocityFieldLattice->FillBuffer( zeroVector );

      typename OutputTransformType::VelocityFieldPointType        sampledVelocityFieldOrigin;
      typename OutputTransformType::VelocityFieldSpacingType      sampledVelocityFieldSpacing;
      typename OutputTransformType::VelocityFieldSizeType         sampledVelocityFieldSize;
      typename OutputTransformType::VelocityFieldDirectionType    sampledVelocityFieldDirection;

      sampledVelocityFieldOrigin.Fill( 0.0 );
      sampledVelocityFieldSpacing.Fill( 1.0 );
      sampledVelocityFieldSize.Fill( numberOfTimePointSamples );
      sampledVelocityFieldDirection.SetIdentity();
      for( unsigned int i = 0; i < VImageDimension; i++ )
        {
        sampledVelocityFieldOrigin[i] = fixedImage->GetOrigin()[i];
        sampledVelocityFieldSpacing[i] = fixedImage->GetSpacing()[i];
        sampledVelocityFieldSize[i] = fixedImage->GetRequestedRegion().GetSize()[i];
        for( unsigned int j = 0; j < VImageDimension; j++ )
          {
          sampledVelocityFieldDirection[i][j] = fixedImage->GetDirection()[i][j];
          }
        }

      outputTransform->SetTimeVaryingVelocityFieldControlPointLattice( velocityFieldLattice );
      outputTransform->SetVelocityFieldOrigin( sampledVelocityFieldOrigin );
      outputTransform->SetVelocityFieldDirection( sampledVelocityFieldDirection );
      outputTransform->SetVelocityFieldSpacing( sampledVelocityFieldSpacing );
      outputTransform->SetVelocityFieldSize( sampledVelocityFieldSize );
//      velocityFieldRegistration->GetOutput()->Get()->IntegrateVelocityField();

      typename VelocityFieldRegistrationType::NumberOfIterationsArrayType numberOfIterationsPerLevel;
      numberOfIterationsPerLevel.SetSize( numberOfLevels );
      for( unsigned int d = 0; d < numberOfLevels; d++ )
        {
        numberOfIterationsPerLevel[d] = iterations[d];
        }
      velocityFieldRegistration->SetNumberOfIterationsPerLevel( numberOfIterationsPerLevel );
      velocityFieldRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      velocityFieldRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );

      typename VelocityFieldRegistrationType::TransformParametersAdaptorsContainerType adaptors;
      for( unsigned int level = 0; level < shrinkFactorsPerLevel.Size(); level++ )
        {
        typename VelocityFieldTransformAdaptorType::Pointer fieldTransformAdaptor =
          VelocityFieldTransformAdaptorType::New();
        fieldTransformAdaptor->SetTransform( outputTransform );
        fieldTransformAdaptor->SetRequiredTransformDomainOrigin( transformDomainOrigin );
        fieldTransformAdaptor->SetRequiredTransformDomainMeshSize( transformDomainMeshSize );
        fieldTransformAdaptor->SetRequiredTransformDomainDirection( transformDomainDirection );
        fieldTransformAdaptor->SetRequiredTransformDomainPhysicalDimensions( transformDomainPhysicalDimensions );

        adaptors.push_back( fieldTransformAdaptor.GetPointer() );
        for( unsigned int i = 0; i <= VImageDimension; i++ )
          {
          transformDomainMeshSize[i] <<= 1;
          }

        }
      velocityFieldRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );

      typedef CommandIterationUpdate<VelocityFieldRegistrationType> VelocityFieldCommandType;
      typename VelocityFieldCommandType::Pointer velocityFieldRegistrationObserver = VelocityFieldCommandType::New();
      velocityFieldRegistrationObserver->SetNumberOfIterations( iterations );

      velocityFieldRegistration->AddObserver( itk::IterationEvent(), velocityFieldRegistrationObserver );

      try
        {
        std::cout << std::endl
                  << "*** Running time-varying b-spline velocity field registration (initial mesh size = "
                  << initialTransformDomainMeshSize << ") ***" << std::endl << std::endl;
        velocityFieldRegistrationObserver->Execute( velocityFieldRegistration, itk::StartEvent() );
        velocityFieldRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }
      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputTransform );

      // Write out the displacement fields

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Warp.nii.gz" );

      typedef typename OutputTransformType::DisplacementFieldType DisplacementFieldType;

      typedef itk::ImageFileWriter<DisplacementFieldType> WriterType;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput( outputTransform->GetDisplacementField() );
      writer->SetFileName( filename.c_str() );
      writer->Update();

      std::string inverseFilename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "InverseWarp.nii.gz" );

      typedef itk::ImageFileWriter<DisplacementFieldType> InverseWriterType;
      typename InverseWriterType::Pointer inverseWriter = InverseWriterType::New();
      inverseWriter->SetInput( outputTransform->GetInverseDisplacementField() );
      inverseWriter->SetFileName( inverseFilename.c_str() );
      inverseWriter->Update();
      }
      break;
      case Syn:
      {
      typedef itk::Vector<RealType, VImageDimension> VectorType;
      VectorType zeroVector( 0.0 );
      typedef itk::Image<VectorType, VImageDimension> DisplacementFieldType;
      typename DisplacementFieldType::Pointer displacementField = DisplacementFieldType::New();
      displacementField->CopyInformation( fixedImage );
      displacementField->SetRegions( fixedImage->GetBufferedRegion() );
      displacementField->Allocate();
      displacementField->FillBuffer( zeroVector );

      typename DisplacementFieldType::Pointer inverseDisplacementField = DisplacementFieldType::New();
      inverseDisplacementField->CopyInformation( fixedImage );
      inverseDisplacementField->SetRegions( fixedImage->GetBufferedRegion() );
      inverseDisplacementField->Allocate();
      inverseDisplacementField->FillBuffer( zeroVector );

      typedef itk::DisplacementFieldTransform<RealType, VImageDimension> DisplacementFieldTransformType;

      typedef itk::SyNImageRegistrationMethod<ImageType, ImageType,
                                              DisplacementFieldTransformType> DisplacementFieldRegistrationType;
      typename DisplacementFieldRegistrationType::Pointer displacementFieldRegistration =
        DisplacementFieldRegistrationType::New();

      typename DisplacementFieldTransformType::Pointer outputDisplacementFieldTransform =
        const_cast<DisplacementFieldTransformType *>( displacementFieldRegistration->GetOutput()->Get() );

      // Create the transform adaptors

      typedef itk::DisplacementFieldTransformParametersAdaptor<DisplacementFieldTransformType>
      DisplacementFieldTransformAdaptorType;
      typename DisplacementFieldRegistrationType::TransformParametersAdaptorsContainerType adaptors;
      // Create the transform adaptors
      // For the gaussian displacement field, the specified variances are in image spacing terms
      // and, in normal practice, we typically don't change these values at each level.  However,
      // if the user wishes to add that option, they can use the class
      // GaussianSmoothingOnUpdateDisplacementFieldTransformAdaptor
      for( unsigned int level = 0; level < numberOfLevels; level++ )
        {
        // We use the shrink image filter to calculate the fixed parameters of the virtual
        // domain at each level.  To speed up calculation and avoid unnecessary memory
        // usage, we could calculate these fixed parameters directly.

        typedef itk::ShrinkImageFilter<DisplacementFieldType, DisplacementFieldType> ShrinkFilterType;
        typename ShrinkFilterType::Pointer shrinkFilter = ShrinkFilterType::New();
        shrinkFilter->SetShrinkFactors( shrinkFactorsPerLevel[level] );
        shrinkFilter->SetInput( displacementField );
        shrinkFilter->Update();

        typename DisplacementFieldTransformAdaptorType::Pointer fieldTransformAdaptor =
          DisplacementFieldTransformAdaptorType::New();
        fieldTransformAdaptor->SetRequiredSpacing( shrinkFilter->GetOutput()->GetSpacing() );
        fieldTransformAdaptor->SetRequiredSize( shrinkFilter->GetOutput()->GetBufferedRegion().GetSize() );
        fieldTransformAdaptor->SetRequiredDirection( shrinkFilter->GetOutput()->GetDirection() );
        fieldTransformAdaptor->SetRequiredOrigin( shrinkFilter->GetOutput()->GetOrigin() );
        fieldTransformAdaptor->SetTransform( outputDisplacementFieldTransform );

        adaptors.push_back( fieldTransformAdaptor.GetPointer() );
        }

      // Extract parameters
      typename DisplacementFieldRegistrationType::NumberOfIterationsArrayType numberOfIterationsPerLevel;
      numberOfIterationsPerLevel.SetSize( numberOfLevels );
      for( unsigned int d = 0; d < numberOfLevels; d++ )
        {
        numberOfIterationsPerLevel[d] = iterations[d];
        }

      RealType varianceForUpdateField = this->m_TransformMethods[currentStage].m_UpdateFieldSigmaInPhysicalSpace;
      RealType varianceForTotalField = this->m_TransformMethods[currentStage].m_TotalFieldSigmaInPhysicalSpace;

      displacementFieldRegistration->SetDownsampleImagesForMetricDerivatives( true );
      displacementFieldRegistration->SetAverageMidPointGradients( false );
      displacementFieldRegistration->SetFixedImage( preprocessFixedImage );
      displacementFieldRegistration->SetMovingImage( preprocessMovingImage );
      displacementFieldRegistration->SetMovingInitialTransform( compositeTransform );
      displacementFieldRegistration->SetNumberOfLevels( numberOfLevels );
      displacementFieldRegistration->SetShrinkFactorsPerLevel( shrinkFactorsPerLevel );
      displacementFieldRegistration->SetSmoothingSigmasPerLevel( smoothingSigmasPerLevel );
      displacementFieldRegistration->SetMetric( metric );
      displacementFieldRegistration->SetLearningRate( learningRate );
      displacementFieldRegistration->SetNumberOfIterationsPerLevel( numberOfIterationsPerLevel );
      displacementFieldRegistration->SetTransformParametersAdaptorsPerLevel( adaptors );
      displacementFieldRegistration->SetGaussianSmoothingVarianceForTheUpdateField( varianceForUpdateField );
      displacementFieldRegistration->SetGaussianSmoothingVarianceForTheTotalField( varianceForTotalField );
      outputDisplacementFieldTransform->SetDisplacementField( displacementField );
      outputDisplacementFieldTransform->SetInverseDisplacementField( inverseDisplacementField );

      typedef CommandIterationUpdate<DisplacementFieldRegistrationType> DisplacementFieldCommandType;
      typename DisplacementFieldCommandType::Pointer displacementFieldRegistrationObserver =
        DisplacementFieldCommandType::New();
      displacementFieldRegistrationObserver->SetNumberOfIterations( iterations );

      displacementFieldRegistration->AddObserver( itk::IterationEvent(), displacementFieldRegistrationObserver );

      try
        {
        std::cout << std::endl << "*** Running SyN registration (varianceForUpdateField = "
                  << varianceForUpdateField << ", varianceForTotalField = " << varianceForTotalField << ") ***"
                  << std::endl << std::endl;
        displacementFieldRegistrationObserver->Execute( displacementFieldRegistration, itk::StartEvent() );
        displacementFieldRegistration->StartRegistration();
        }
      catch( itk::ExceptionObject & e )
        {
        std::cerr << "Exception caught: " << e << std::endl;
        return EXIT_FAILURE;
        }

      // Add calculated transform to the composite transform
      compositeTransform->AddTransform( outputDisplacementFieldTransform );

      // Write out the displacement field and its inverse

      std::string filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "Warp.nii.gz" );

      typedef itk::ImageFileWriter<DisplacementFieldType> WriterType;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput( outputDisplacementFieldTransform->GetDisplacementField() );
      writer->SetFileName( filename.c_str() );
      writer->Update();

      filename = this->m_OutputTransformPrefix + currentStageString.str() + std::string( "InverseWarp.nii.gz" );

      typename WriterType::Pointer inverseWriter = WriterType::New();
      inverseWriter->SetInput( outputDisplacementFieldTransform->GetInverseDisplacementField() );
      inverseWriter->SetFileName( filename.c_str() );
      inverseWriter->Update();
      }
      break;
      default:
      std::cerr << "ERROR:  Unrecognized transform option - " << whichTransform << std::endl;
      return EXIT_FAILURE;
      }
    timer.Stop();
    std::cout << "  Elapsed time (stage "
              << ( this->m_NumberOfStages - currentStage - 1 ) << "): " << timer.GetMeanTime() << std::endl << std::endl;
    }

  return EXIT_SUCCESS;
}


int
RegistrationHelper
::DoRegistration()
{
  switch(this->m_ImageDimension)
    {
    case 2:
      return this->DoRegistrationInternal<2>();
    case 3:
      return this->DoRegistrationInternal<3>();
    default:
      break;
    }
  return EXIT_FAILURE;
}

} // namespace ants
} // namespace itk

#endif // __itkantsRegistrationHelper_hxx
