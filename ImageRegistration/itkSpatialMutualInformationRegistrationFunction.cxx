/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkSpatialMutualInformationRegistrationFunction.cxx,v $
  Language:  C++
  Date:      $Date: 2009/01/08 15:14:48 $
  Version:   $Revision: 1.21 $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkSpatialMutualInformationRegistrationFunction_txx
#define _itkSpatialMutualInformationRegistrationFunction_txx

#include "itkSpatialMutualInformationRegistrationFunction.h"
#include "itkBSplineInterpolateImageFunction.h"
#include "itkCovariantVector.h"
#include "itkImageRandomConstIteratorWithIndex.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionIterator.h"
#include "itkImageIterator.h"
#include "vnl/vnl_math.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkBSplineDeformableTransform.h"
#include "itkImageRegionConstIteratorWithIndex.h"

namespace itk
{
/**
 * Constructor
 */
template <class TFixedImage, class TMovingImage, class TDeformationField>
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::SpatialMutualInformationRegistrationFunction()
{
  this->Superclass::m_NormalizeGradient = true;
  this->m_NumberOfSpatialSamples = 5000;
  this->m_NumberOfHistogramBins = 50;

  //  this->SetComputeGradient(false); // don't use the default gradient for now

  this->m_InterpolatorIsBSpline = false;
  this->m_TransformIsBSpline    = false;

  // Initialize PDFs to NULL
  m_JointPDF = NULL;
  m_JointPDFDerivatives = NULL;

  m_OpticalFlow = false;
  typename TransformType::Pointer transformer = TransformType::New();
  this->SetTransform(transformer);

  typename BSplineInterpolatorType::Pointer interpolator = BSplineInterpolatorType::New();
  this->SetInterpolator(interpolator);

  m_FixedImageMask = NULL;
  m_MovingImageMask = NULL;

  // Initialize memory
  m_MovingImageNormalizedMin = 0.0;
  m_FixedImageNormalizedMin = 0.0;
  m_MovingImageTrueMin = 0.0;
  m_MovingImageTrueMax = 0.0;
  m_FixedImageBinSize = 0.0;
  m_MovingImageBinSize = 0.0;
  m_CubicBSplineDerivativeKernel = NULL;
  m_BSplineInterpolator = NULL;
  m_DerivativeCalculator = NULL;
  m_NumParametersPerDim = 0;
  m_NumBSplineWeights = 0;
  m_BSplineTransform = NULL;
  m_NumberOfParameters = ImageDimension;

  m_FixedImageGradientCalculator = GradientCalculatorType::New();
  m_MovingImageGradientCalculator = GradientCalculatorType::New();
  this->m_Padding = 2;

  typename DefaultInterpolatorType::Pointer interp =  DefaultInterpolatorType::New();
  typename DefaultInterpolatorType::Pointer interp2 = DefaultInterpolatorType::New();

  m_MovingImageInterpolator = static_cast<InterpolatorType *>(
      interp.GetPointer() );
  m_FixedImageInterpolator = static_cast<InterpolatorType *>(
      interp2.GetPointer() );
  m_Interpolator = static_cast<InterpolatorType *>(
      interp.GetPointer() );

  this->m_RobustnessParameter = -1.e19;
}

/**
 * Print out internal information about this class
 */
template <class TFixedImage, class TMovingImage, class TDeformationField>
void
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "NumberOfSpatialSamples: ";
  os << m_NumberOfSpatialSamples << std::endl;
  os << indent << "NumberOfHistogramBins: ";
  os << m_NumberOfHistogramBins << std::endl;

  // Debugging information
  os << indent << "NumberOfParameters: ";
  os << m_NumberOfParameters << std::endl;
  os << indent << "FixedImageNormalizedMin: ";
  os << m_FixedImageNormalizedMin << std::endl;
  os << indent << "MovingImageNormalizedMin: ";
  os << m_MovingImageNormalizedMin << std::endl;
  os << indent << "MovingImageTrueMin: ";
  os << m_MovingImageTrueMin << std::endl;
  os << indent << "MovingImageTrueMax: ";
  os << m_MovingImageTrueMax << std::endl;
  os << indent << "FixedImageBinSize: ";
  os << m_FixedImageBinSize << std::endl;
  os << indent << "MovingImageBinSize: ";
  os << m_MovingImageBinSize << std::endl;
  os << indent << "InterpolatorIsBSpline: ";
  os << m_InterpolatorIsBSpline << std::endl;
  os << indent << "TransformIsBSpline: ";
  os << m_TransformIsBSpline << std::endl;
}

/**
 * Initialize
 */
template <class TFixedImage, class TMovingImage, class TDeformationField>
void
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::InitializeIteration()
{
  m_CubicBSplineKernel = CubicBSplineFunctionType::New();
  m_CubicBSplineDerivativeKernel = CubicBSplineDerivativeFunctionType::New();
  this->m_Energy = 0;
  this->pdfinterpolator = pdfintType::New();
  this->pdfinterpolatorXuY = pdfintType::New();
  this->pdfinterpolatorXYu = pdfintType::New();
  this->pdfinterpolatorXlY = pdfintType::New();
  this->pdfinterpolatorXYl = pdfintType::New();
  this->pdfinterpolatorXuYl = pdfintType::New();
  this->pdfinterpolatorXlYu = pdfintType::New();
  this->pdfinterpolatorXuYr = pdfintType::New();
  this->pdfinterpolatorXrYu = pdfintType::New();

  pdfinterpolator2 = pdfintType2::New();
  pdfinterpolator3 = pdfintType2::New();
  m_DerivativeCalculator = DerivativeFunctionType::New();

//  this->ComputeMetricImage();
  std::cout << " A " << std::endl;
//    bool makenewimage=false;
  typedef ImageRegionIteratorWithIndex<TFixedImage> ittype;
  TFixedImage* img = const_cast<TFixedImage *>(this->m_FixedImage.GetPointer() );
  typename TFixedImage::SizeType imagesize = img->GetLargestPossibleRegion().GetSize();
  std::cout << " B " << std::endl;

  /*
  if (!this->m_MetricImage )makenewimage=true;
  else if (imagesize[0] != this->m_MetricImage->GetLargestPossibleRegion().GetSize()[0])
    makenewimage = true;
  else this->m_MetricImage->FillBuffer(0);
  if (makenewimage)
  {
    this->m_MetricImage = TFixedImage::New();
    this->m_MetricImage->SetLargestPossibleRegion(img->GetLargestPossibleRegion()  );
    this->m_MetricImage->SetBufferedRegion(img->GetLargestPossibleRegion());
    this->m_MetricImage->SetSpacing(img->GetSpacing());
    this->m_MetricImage->SetOrigin(img->GetOrigin());
    this->m_MetricImage->Allocate();
    ittype it(this->m_MetricImage,this->m_MetricImage->GetLargestPossibleRegion().GetSize());
    for( it.GoToBegin(); !it.IsAtEnd(); ++it ) it.Set(0);
  }
  */
  m_FixedImageGradientCalculator->SetInputImage( this->m_FixedImage );
  m_MovingImageGradientCalculator->SetInputImage( this->m_MovingImage );
  m_FixedImageInterpolator->SetInputImage( this->m_FixedImage );
  m_Interpolator->SetInputImage( this->m_MovingImage );

  m_FixedImageSpacing    = this->m_FixedImage->GetSpacing();
  m_FixedImageOrigin     = this->m_FixedImage->GetOrigin();
  m_Normalizer      = 0.0;
  m_NumberOfSpatialSamples = 1;
  for( unsigned int k = 0; k < ImageDimension; k++ )
    {
    m_Normalizer += m_FixedImageSpacing[k] * m_FixedImageSpacing[k];
    m_NumberOfSpatialSamples *= this->m_FixedImage->GetLargestPossibleRegion().GetSize()[k];
    }
  m_Normalizer /= static_cast<double>( ImageDimension );

  /**
   * Compute binsize for the histograms.
   *
   * The binsize for the image intensities needs to be adjusted so that
   * we can avoid dealing with boundary conditions using the cubic
   * spline as the Parzen window.  We do this by increasing the size
   * of the bins so that the joint histogram becomes "padded" at the
   * borders. Because we are changing the binsize,
   * we also need to shift the minimum by the padded amount in order to
   * avoid minimum values filling in our padded region.
   *
   * Note that there can still be non-zero bin values in the padded region,
   * it's just that these bins will never be a central bin for the Parzen
   * window.
   *
  double fixedImageMax = 1.0;
  double fixedImageMin = 0.0;
  double movingImageMax = 1.0;
  double movingImageMin = 0.0;
  m_MovingImageTrueMin = movingImageMin;
  m_MovingImageTrueMax = movingImageMax;

   */

  double movingImageMin = NumericTraits<double>::max();
  double movingImageMax = NumericTraits<double>::NonpositiveMin();
  double fixedImageMin = NumericTraits<double>::max();
  double fixedImageMax = NumericTraits<double>::NonpositiveMin();

  typedef ImageRegionConstIterator<MovingImageType> MovingIteratorType;
  MovingIteratorType movingImageIterator(
    this->m_MovingImage, this->m_MovingImage->GetBufferedRegion() );
  for( movingImageIterator.GoToBegin();
       !movingImageIterator.IsAtEnd(); ++movingImageIterator )
    {
    bool takesample = true;

    if( this->m_FixedImageMask )
      {
      if( this->m_FixedImageMask->GetPixel( movingImageIterator.GetIndex() ) < 1.e-6 )
        {
        takesample = false;
        }
      }

    if( takesample )
      {
      double sample = static_cast<double>( movingImageIterator.Get() );
      double fsample = static_cast<double>( this->m_FixedImage->GetPixel( movingImageIterator.GetIndex() ) );

      if( sample < movingImageMin )
        {
        movingImageMin = sample;
        }

      if( sample > movingImageMax )
        {
        movingImageMax = sample;
        }

      if( fsample < fixedImageMin )
        {
        fixedImageMin = fsample;
        }

      if( fsample > fixedImageMax )
        {
        fixedImageMax = fsample;
        }
      }
    }
  this->m_MovingImageTrueMax = movingImageMax;
  this->m_FixedImageTrueMax = fixedImageMax;
  this->m_MovingImageTrueMin = movingImageMin;
  this->m_FixedImageTrueMin = fixedImageMin;
  fixedImageMax = 1. * ( fixedImageMax - fixedImageMin ) + fixedImageMin;
  movingImageMax = 1. * ( movingImageMax - movingImageMin ) + movingImageMin;

  m_FixedImageBinSize = ( fixedImageMax - fixedImageMin )
    / static_cast<double>( m_NumberOfHistogramBins - 2 * this->m_Padding );
  m_FixedImageNormalizedMin = fixedImageMin / m_FixedImageBinSize
    - static_cast<double>( this->m_Padding );

  m_MovingImageBinSize = ( movingImageMax - movingImageMin )
    / static_cast<double>( m_NumberOfHistogramBins - 2 * this->m_Padding );
  m_MovingImageNormalizedMin = movingImageMin / m_MovingImageBinSize
    - static_cast<double>( this->m_Padding );

  m_FixedImageMarginalPDF = MarginalPDFType::New();
  m_MovingImageMarginalPDF = MarginalPDFType::New();
  typename MarginalPDFType::RegionType            mPDFRegion;
  typename MarginalPDFType::SizeType              mPDFSize;
  typename MarginalPDFType::IndexType              mPDFIndex;
  typename MarginalPDFType::SpacingType              mPDFspacing;
  mPDFspacing.Fill(1);
  mPDFIndex.Fill( 0 );
  mPDFSize.Fill( m_NumberOfHistogramBins );
  mPDFRegion.SetIndex( mPDFIndex );
  mPDFRegion.SetSize( mPDFSize );
  m_FixedImageMarginalPDF->SetRegions( mPDFRegion );
  m_FixedImageMarginalPDF->Allocate();
  m_FixedImageMarginalPDF->SetSpacing(mPDFspacing);
  m_MovingImageMarginalPDF->SetRegions( mPDFRegion );
  m_MovingImageMarginalPDF->Allocate();
  m_MovingImageMarginalPDF->SetSpacing(mPDFspacing);
  std::cout << " C " << std::endl;

  /**
   * Allocate memory for the joint PDF and joint PDF derivatives.
   * The joint PDF and joint PDF derivatives are store as itk::Image.
   */
  this->m_JointPDF = JointPDFType::New();
  this->m_JointPDFXuY = JointPDFType::New();
  this->m_JointPDFXYu = JointPDFType::New();
  this->m_JointPDFXlY = JointPDFType::New();
  this->m_JointPDFXYl = JointPDFType::New();
  this->m_JointPDFXuYl = JointPDFType::New();
  this->m_JointPDFXlYu = JointPDFType::New();
  this->m_JointPDFXrYu = JointPDFType::New();
  this->m_JointPDFXuYr = JointPDFType::New();

  std::cout << " D " << std::endl;

  // Instantiate a region, index, size
  JointPDFRegionType jointPDFRegion;
  JointPDFIndexType  jointPDFIndex;
  JointPDFSizeType   jointPDFSize;
  typename JointPDFType::SpacingType jspacing; jspacing.Fill(1);

  // For the joint PDF define a region starting from {0,0}
  // with size {m_NumberOfHistogramBins, m_NumberOfHistogramBins}.
  // The dimension represents fixed image parzen window index
  // and moving image parzen window index, respectively.
  jointPDFIndex.Fill( 0 );
  jointPDFSize.Fill( m_NumberOfHistogramBins );
  jointPDFRegion.SetIndex( jointPDFIndex );
  jointPDFRegion.SetSize( jointPDFSize );

  m_JointHist = JointPDFType::New();
  m_JointHist->SetRegions(jointPDFRegion );
  m_JointHist->Allocate();
  this->m_JointHist->SetSpacing(jspacing);

  // Set the regions and allocate
  this->m_JointPDF->SetRegions( jointPDFRegion );
  this->m_JointPDF->Allocate();
  this->m_JointPDF->SetSpacing(jspacing);

  this->m_JointPDFXYu->SetRegions( jointPDFRegion );
  this->m_JointPDFXYu->Allocate();
  this->m_JointPDFXYu->SetSpacing(jspacing);

  this->m_JointPDFXuY->SetRegions( jointPDFRegion );
  this->m_JointPDFXuY->Allocate();
  this->m_JointPDFXuY->SetSpacing(jspacing);

  this->m_JointPDFXlY->SetRegions( jointPDFRegion );
  this->m_JointPDFXlY->Allocate();
  this->m_JointPDFXlY->SetSpacing(jspacing);

  this->m_JointPDFXYl->SetRegions( jointPDFRegion );
  this->m_JointPDFXYl->Allocate();
  this->m_JointPDFXYl->SetSpacing(jspacing);

  //
  this->m_JointPDFXlYu->SetRegions( jointPDFRegion );
  this->m_JointPDFXlYu->Allocate();
  this->m_JointPDFXlYu->SetSpacing(jspacing);

  this->m_JointPDFXuYl->SetRegions( jointPDFRegion );
  this->m_JointPDFXuYl->Allocate();
  this->m_JointPDFXuYl->SetSpacing(jspacing);

  this->m_JointPDFXrYu->SetRegions( jointPDFRegion );
  this->m_JointPDFXrYu->Allocate();
  this->m_JointPDFXrYu->SetSpacing(jspacing);

  this->m_JointPDFXuYr->SetRegions( jointPDFRegion );
  this->m_JointPDFXuYr->Allocate();
  this->m_JointPDFXuYr->SetSpacing(jspacing);

  std::cout << " E " << std::endl;

  m_NormalizeMetric = 1.0;
  for( int i = 0; i < ImageDimension; i++ )
    {
    m_NormalizeMetric *= this->m_FixedImage->GetLargestPossibleRegion().GetSize()[i];
    }

  std::cout << " F " << std::endl;
  pdfinterpolator->SetInputImage(m_JointPDF);
  pdfinterpolator->SetSplineOrder(3);
  this->pdfinterpolatorXuY->SetInputImage(this->m_JointPDFXuY);
  this->pdfinterpolatorXYu->SetInputImage(this->m_JointPDFXYu);
  this->pdfinterpolatorXlY->SetInputImage(this->m_JointPDFXlY);
  this->pdfinterpolatorXYl->SetInputImage(this->m_JointPDFXYl);
  this->pdfinterpolatorXuYl->SetInputImage(this->m_JointPDFXuYl);
  this->pdfinterpolatorXlYu->SetInputImage(this->m_JointPDFXlYu);
  this->pdfinterpolatorXuYr->SetInputImage(this->m_JointPDFXuYr);
  this->pdfinterpolatorXrYu->SetInputImage(this->m_JointPDFXrYu);
  this->pdfinterpolatorXuY->SetSplineOrder(3);
  this->pdfinterpolatorXYu->SetSplineOrder(3);
  this->pdfinterpolatorXlY->SetSplineOrder(3);
  this->pdfinterpolatorXYl->SetSplineOrder(3);
  this->pdfinterpolatorXuYl->SetSplineOrder(3);
  this->pdfinterpolatorXlYu->SetSplineOrder(3);
  this->pdfinterpolatorXuYr->SetSplineOrder(3);
  this->pdfinterpolatorXrYu->SetSplineOrder(3);
  pdfinterpolator2->SetInputImage(m_FixedImageMarginalPDF);
  pdfinterpolator3->SetInputImage(m_MovingImageMarginalPDF);
  pdfinterpolator2->SetSplineOrder(3);
  pdfinterpolator3->SetSplineOrder(3);

  std::cout << " Ga " << std::endl;

  this->GetProbabilities();
  std::cout << " G " << std::endl;
  this->ComputeMutualInformation();
  std::cout << " H " << std::endl;
}

/**
 * Get the both Value and Derivative Measure
 */
template <class TFixedImage, class TMovingImage, class TDeformationField>
void
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::GetProbabilities()
{
  typedef ImageRegionConstIteratorWithIndex<FixedImageType> RandomIterator;
  RandomIterator randIter( this->m_FixedImage, this->m_FixedImage->GetLargestPossibleRegion() );
  for( unsigned int j = 0; j < m_NumberOfHistogramBins; j++ )
    {
    MarginalPDFIndexType mind;
    mind[0] = j;
    m_FixedImageMarginalPDF->SetPixel(mind, 0);
    m_MovingImageMarginalPDF->SetPixel(mind, 0);
    }

  // Reset the joint pdfs to zero
  m_JointPDF->FillBuffer( 0.0 );
  m_JointPDFXuY->FillBuffer( 0.0 );
  m_JointPDFXYu->FillBuffer( 0.0 );
  m_JointPDFXlY->FillBuffer( 0.0 );
  m_JointPDFXYl->FillBuffer( 0.0 );
  m_JointPDFXuYl->FillBuffer( 0.0 );
  m_JointPDFXlYu->FillBuffer( 0.0 );
  m_JointPDFXrYu->FillBuffer( 0.0 );
  m_JointPDFXuYr->FillBuffer( 0.0 );
  m_JointHist->FillBuffer( 0.0 );
  //  m_JointPDFDerivatives->FillBuffer( 0.0 );

  unsigned long  nSamples = 0;
  RandomIterator iter( this->m_FixedImage, this->m_FixedImage->GetLargestPossibleRegion() );
  for( iter.GoToBegin(); !iter.IsAtEnd(); ++iter )
    {
    bool takesample = true;
    if( this->m_FixedImageMask )
      {
      if( this->m_FixedImageMask->GetPixel( iter.GetIndex() ) < 1.e-6 )
        {
        takesample = false;
        }
      }

    if( takesample )
      {
      // Get sampled index
      FixedImageIndexType index = iter.GetIndex();
      FixedImageIndexType IndexU = index;
      FixedImageIndexType IndexL = index;
      FixedImageIndexType IndexR = index;

      // check the neighboring voxel to be in the image
      typename FixedImageType::SizeType imagesize = this->m_FixedImage->GetLargestPossibleRegion().GetSize();
      bool inimage = true;
      for( unsigned int dd = 0; dd < ImageDimension; dd++ )
        {
        if( index[dd] - 1 < 1 || index[dd] >= static_cast<typename IndexType::IndexValueType>(imagesize[dd] - 1) )
          {
          inimage = false;
          }
        }

      if( inimage )
        {
        IndexU[0] = index[0] - 1;
        IndexL[1] = index[1] - 1;
        IndexR[1] = index[1] + 1;

        double       movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( index )  );
        double       fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( index )  );
        unsigned int movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        unsigned int fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        JointPDFValueType *pdfPtr = m_JointPDF->GetBufferPointer()
          + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        int pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( index )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexU )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXuY->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexU )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( index )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXYu->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( index )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexL )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXlY->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexL )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( index )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXYl->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexL )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexU )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXuYl->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexU )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexL )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXlYu->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexU )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexR )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXrYu->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( IndexR )  );
        fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( IndexU )  );
        movingImageParzenWindowIndex = this->FitIndexInBins(  movingImageValue );
        fixedImageParzenWindowIndex = this->FitIndexInBins( fixedImageValue  );

        pdfPtr = m_JointPDFXuYr->GetBufferPointer() + ( fixedImageParzenWindowIndex * m_NumberOfHistogramBins );
        pdfMovingIndex = static_cast<int>( movingImageParzenWindowIndex );
        pdfPtr += pdfMovingIndex;
        *(pdfPtr) += static_cast<PDFValueType>( 1 );

        ++nSamples;
        }
      }
    }

  /**
   * Normalize the PDFs, compute moving image marginal PDF
   *
   */
  typedef ImageRegionIterator<JointPDFType> JointPDFIteratorType;
  JointPDFIteratorType jointPDFIterator( m_JointPDF, m_JointPDF->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXuYIterator( m_JointPDFXuY, m_JointPDFXuY->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXYuIterator( m_JointPDFXYu, m_JointPDFXYu->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXlYIterator( m_JointPDFXlY, m_JointPDFXlY->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXYlIterator( m_JointPDFXYl, m_JointPDFXYl->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXuYlIterator( m_JointPDFXuYl, m_JointPDFXuYl->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXlYuIterator( m_JointPDFXlYu, m_JointPDFXlYu->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXrYuIterator( m_JointPDFXrYu, m_JointPDFXrYu->GetBufferedRegion() );
  JointPDFIteratorType jointPDFXuYrIterator( m_JointPDFXuYr, m_JointPDFXuYr->GetBufferedRegion() );

  // Compute joint PDF normalization factor (to ensure joint PDF sum adds to 1.0)
  double jointPDFSum = 0.0;
  float  max = 0;
  float  max2 = 0;
  float  max3 = 0;
  float  max4 = 0;
  jointPDFIterator.GoToBegin();
  while( !jointPDFIterator.IsAtEnd() )
    {
    float temp = jointPDFIterator.Get();
    m_JointHist->SetPixel(jointPDFIterator.GetIndex(), temp);
    if( temp > max )
      {
      max = temp;
      }
    if( temp > max2 && temp < max )
      {
      max2 = temp;
      }
    if( temp > max3 && temp < max2 )
      {
      max3 = temp;
      }
    if( temp > max4 && temp < max3 )
      {
      max4 = temp;
      }
    ++jointPDFIterator;
    }

  jointPDFIterator.GoToBegin();
  while( !jointPDFIterator.IsAtEnd() )
    {
    float temp = jointPDFIterator.Get();
    jointPDFIterator.Set(temp);
    jointPDFSum += temp;
    ++jointPDFIterator;
    }

  // std::cout << " Joint PDF Summation? " << jointPDFSum << std::endl;

// of derivatives
  if( jointPDFSum == 0.0 )
    {
    itkExceptionMacro( "Joint PDF summed to zero" );
    }

  // Normalize the PDF bins
  jointPDFIterator.GoToEnd();
  jointPDFXuYIterator.GoToEnd();
  jointPDFXYuIterator.GoToEnd();
  jointPDFXlYIterator.GoToEnd();
  jointPDFXYlIterator.GoToEnd();
  jointPDFXuYlIterator.GoToEnd();
  jointPDFXlYuIterator.GoToEnd();
  jointPDFXrYuIterator.GoToEnd();
  jointPDFXuYrIterator.GoToEnd();

  while( !jointPDFIterator.IsAtBegin() )
    {
    --jointPDFIterator;
    --jointPDFXuYIterator;
    --jointPDFXYuIterator;
    --jointPDFXlYIterator;
    --jointPDFXYlIterator;
    --jointPDFXuYlIterator;
    --jointPDFXlYuIterator;
    --jointPDFXrYuIterator;
    --jointPDFXuYrIterator;

    jointPDFIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXuYIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXYuIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXlYIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXYlIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXuYlIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXlYuIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXrYuIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    jointPDFXuYrIterator.Value() /= static_cast<PDFValueType>( jointPDFSum );
    }

  bool smoothjh = true;
  if( smoothjh )
    {
    typedef DiscreteGaussianImageFilter<JointPDFType, JointPDFType> dgtype;
    typename dgtype::Pointer dg = dgtype::New();
    dg->SetInput(this->m_JointPDF);
    dg->SetVariance(1.);
    dg->SetUseImageSpacingOff();
    dg->SetMaximumError(.01f);
    dg->Update();
    this->m_JointPDF = dg->GetOutput();
    }

  // Compute moving image marginal PDF by summing over fixed image bins.
  typedef ImageLinearIteratorWithIndex<JointPDFType> JointPDFLinearIterator;
  JointPDFLinearIterator linearIter(
    m_JointPDF, m_JointPDF->GetBufferedRegion() );

  linearIter.SetDirection( 0 );
  linearIter.GoToBegin();
  unsigned int fixedIndex = 0;
  while( !linearIter.IsAtEnd() )
    {
    double sum = 0.0;
    while( !linearIter.IsAtEndOfLine() )
      {
      sum += linearIter.Get();
      ++linearIter;
      }

    MarginalPDFIndexType mind;
    mind[0] = fixedIndex;
    m_FixedImageMarginalPDF->SetPixel(mind, static_cast<PDFValueType>(sum) );
    linearIter.NextLine();
    ++fixedIndex;
    }

  linearIter.SetDirection( 1 );
  linearIter.GoToBegin();
  unsigned int movingIndex = 0;
  while( !linearIter.IsAtEnd() )
    {
    double sum = 0.0;
    while( !linearIter.IsAtEndOfLine() )
      {
      sum += linearIter.Get();
      ++linearIter;
      }

    MarginalPDFIndexType mind;
    mind[0] = movingIndex;
    m_MovingImageMarginalPDF->SetPixel(mind, static_cast<PDFValueType>(sum) );
    linearIter.NextLine();
    ++movingIndex;
    }
}

/**
 * Get the both Value and Derivative Measure
 */
template <class TFixedImage, class TMovingImage, class TDeformationField>
double
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::GetValueAndDerivative(IndexType oindex,
                        MeasureType& valuei,
                        DerivativeType& derivative1, DerivativeType& derivative2)
{
  double         value = 0;
  DerivativeType zero(ImageDimension);

  zero.Fill(0);

  double movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( oindex )  );
  double fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( oindex )  );
  double movingImageParzenWindowContIndex = this->FitContIndexInBins(  movingImageValue );
  double fixedImageParzenWindowContIndex = this->FitContIndexInBins( fixedImageValue  );

  double dJPDF = 0, dFmPDF = 0, jointPDFValue = 0, fixedImagePDFValue = 0;

    {
    typename  pdfintType::ContinuousIndexType pdfind;
    pdfind[1] = fixedImageParzenWindowContIndex;
    pdfind[0] = movingImageParzenWindowContIndex;
    jointPDFValue = pdfinterpolator->EvaluateAtContinuousIndex(pdfind);
    dJPDF = (1.0) * (pdfinterpolator->EvaluateDerivativeAtContinuousIndex( pdfind ) )[1];
    }

    {
    typename   pdfintType2::ContinuousIndexType  mind;
    mind[0] = fixedImageParzenWindowContIndex;
    fixedImagePDFValue = pdfinterpolator2->EvaluateAtContinuousIndex(mind);
    dFmPDF = (pdfinterpolator2->EvaluateDerivativeAtContinuousIndex(mind) )[0];
    }

  double term1 = 0, term2 = 0, eps = 1.e-12;
  if( jointPDFValue > eps &&  (fixedImagePDFValue) > 0 )
    {
    term1 = dJPDF / jointPDFValue;
    term2 = dFmPDF / fixedImagePDFValue;
    value =  (term1 * (-1.0) + term2);
    }    // end if-block to check non-zero bin contribution
  else
    {
    value = 0;
    }

  return value;
}

template <class TFixedImage, class TMovingImage, class TDeformationField>
double
SpatialMutualInformationRegistrationFunction<TFixedImage, TMovingImage, TDeformationField>
::GetValueAndDerivativeInv(IndexType oindex,
                           MeasureType& valuei,
                           DerivativeType& derivative1, DerivativeType& derivative2)
{
  double         value = 0;
  DerivativeType zero(ImageDimension);

  zero.Fill(0);

  double movingImageValue = this->GetMovingParzenTerm(  this->m_MovingImage->GetPixel( oindex )  );
  double fixedImageValue = this->GetFixedParzenTerm(  this->m_FixedImage->GetPixel( oindex )  );
  double movingImageParzenWindowContIndex = this->FitContIndexInBins(  movingImageValue );
  double fixedImageParzenWindowContIndex = this->FitContIndexInBins( fixedImageValue  );

  double dJPDF = 0, dMmPDF = 0, jointPDFValue = 0, movingImagePDFValue = 0;

    {
    typename   pdfintType::ContinuousIndexType   pdfind;
    pdfind[1] = fixedImageParzenWindowContIndex;
    pdfind[0] = movingImageParzenWindowContIndex;
    jointPDFValue = pdfinterpolator->EvaluateAtContinuousIndex(pdfind);
    dJPDF = (1.0) * (pdfinterpolator->EvaluateDerivativeAtContinuousIndex( pdfind ) )[0];
    }

    {
    typename   pdfintType2::ContinuousIndexType  mind;
    mind[0] = movingImageParzenWindowContIndex;
    movingImagePDFValue = pdfinterpolator3->EvaluateAtContinuousIndex(mind);
    dMmPDF = (pdfinterpolator3->EvaluateDerivativeAtContinuousIndex(mind) )[0];
    }

  double term1 = 0, term2 = 0, eps = 1.e-12;
  if( jointPDFValue > eps &&  (movingImagePDFValue) > 0 )
    {
    term1 = dJPDF / jointPDFValue;
    term2 = dMmPDF / movingImagePDFValue;
    value =  (term1 * (-1.0) + term2);
    } // end if-block to check non-zero bin contribution
  else
    {
    value = 0;
    }

  // double pmr=jointPDFValue;
  // double pmt=jointPDFValue;
  // double Fx=log( ( pmr / pr )  /  ( pmt / pt  ) );

  return value;
}
} // end namespace itk

#endif