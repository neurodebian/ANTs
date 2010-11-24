/*=========================================================================

  Program:   Advanced Normalization Tools
  Module:    $RCSfile: antsListSampleFunction.h,v $
  Language:  C++
  Date:      $Date: 2008/10/18 00:20:04 $
  Version:   $Revision: 1.1.1.1 $

  Copyright (c) ConsortiumOfANTS. All rights reserved.
  See accompanying COPYING.txt or
  http://sourceforge.net/projects/advants/files/ANTS/ANTSCopyright.txt
  for details.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __antsListSampleFunction_h
#define __antsListSampleFunction_h

#include "itkFunctionBase.h"

#include "itkArray.h"

namespace itk {
namespace ants {
namespace Statistics {

/** \class ListSampleFunction
 * \brief Evaluates a function of an image at specified position.
 *
 * ListSampleFunction is a baseclass for all objects that evaluates
 * a function of a list sample at a measurement
 * This class is templated over the input list type, the type
 * of the function output and the coordinate representation type
 * (e.g. float or double).
 *
 * The input list sample is set via method SetInputListSample().
 * The methods Evaluate() evaluates the function at a measurement vector.
 *
 * \ingroup ListSampleFunctions
 */
template <class TInputListSample, class TOutput, class TCoordRep = float>
class ITK_EXPORT ListSampleFunction :
  public FunctionBase<typename TInputListSample::MeasurementVectorType, TOutput>
{
public:

  /** Standard class typedefs. */
  typedef ListSampleFunction                              Self;
  typedef FunctionBase<TInputListSample, TOutput>         Superclass;
  typedef SmartPointer<Self>                              Pointer;
  typedef SmartPointer<const Self>                        ConstPointer;

  /** Run-time type information (and related methods). */
  itkTypeMacro( ListSampleFunction, FunctionBase );

  /** InputListSampleType typedef support. */
  typedef TInputListSample InputListSampleType;

  /** Array typedef for weights */
  typedef Array<double> WeightArrayType;

  /** InputPixel typedef support */
  typedef typename InputListSampleType::MeasurementVectorType   InputMeasurementVectorType;
  typedef typename InputListSampleType::MeasurementType         InputMeasurementType;

  /** OutputType typedef support. */
  typedef TOutput                                       OutputType;

  /** CoordRepType typedef support. */
  typedef TCoordRep                                     CoordRepType;

  /** Set the input point set.
   * \warning this method caches BufferedRegion information.
   * If the BufferedRegion has changed, user must call
   * SetInputListSample again to update cached values. */
  virtual void SetInputListSample( const InputListSampleType * ptr );

  /** Get the input image. */
  const InputListSampleType * GetInputListSample() const
    { return m_ListSample.GetPointer(); }

  /** Clear the input list sample to free memory */
  virtual void ClearInputListSample()
    { this->SetInputListSample( NULL ); }

  /** Sets the weights using an array */
  virtual void SetWeights( WeightArrayType* array );

  /** Gets the weights array */
  WeightArrayType* GetWeights();

  /** Evaluate the function at specified Point position.
   * Subclasses must provide this method. */
  virtual TOutput Evaluate( const InputMeasurementVectorType& measurement ) const = 0;

protected:
  ListSampleFunction();
  ~ListSampleFunction() {}
  void PrintSelf(std::ostream& os, Indent indent) const;

  /** Const pointer to the input image. */
  typename InputListSampleType::ConstPointer                     m_ListSample;
  WeightArrayType                                                m_Weights;

private:
  ListSampleFunction(const Self&); //purposely not implemented
  void operator=(const Self&); //purposely not implemented

};

} // end of namespace Statistics
} // end of namespace ants
} // end of namespace itk

// Define instantiation macro for this template.
#define ITK_TEMPLATE_ListSampleFunction(_, EXPORT, x, y) namespace itk { \
  _(3(class EXPORT ListSampleFunction< ITK_TEMPLATE_3 x >)) \
  namespace Templates { typedef ListSampleFunction< ITK_TEMPLATE_3 x > ListSampleFunction##y; } \
  }

#if ITK_TEMPLATE_EXPLICIT
# include "Templates/antsListSampleFunction+-.h"
#endif

#if ITK_TEMPLATE_TXX
# include "antsListSampleFunction.txx"
#endif

#endif
