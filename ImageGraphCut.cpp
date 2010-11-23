/*
Copyright (C) 2010 David Doria, daviddoria@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ImageGraphCut.h"

// ITK
#include "itkImageRegionIterator.h"
#include "itkShapedNeighborhoodIterator.h"

// STL
#include <cmath>

// VTK
#include <vtkImageData.h>
#include <vtkMath.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkFloatArray.h>

// Qt
#include <QMessageBox>

template <typename TImageType>
ImageGraphCut<TImageType>::ImageGraphCut(typename TImageType::Pointer image)
{
  this->Image = TImageType::New();
  this->Image->Graft(image);

  // Setup the output (mask) image
  this->SegmentMask = GrayscaleImageType::New();
  this->SegmentMask->SetRegions(this->Image->GetLargestPossibleRegion());
  this->SegmentMask->Allocate();

  // Setup the image to store the node ids
  this->NodeImage = NodeImageType::New();
  this->NodeImage->SetRegions(this->Image->GetLargestPossibleRegion());
  this->NodeImage->Allocate();

  // Default paramters
  this->Lambda = 0.01;
  this->NumberOfHistogramBins = 10; // This value is never used - it is set from the slider

  // Initializations
  this->ForegroundHistogram = NULL;
  this->BackgroundHistogram = NULL;

  this->ForegroundSample = SampleType::New();
  this->BackgroundSample = SampleType::New();

  ForegroundHistogramFilter = SampleToHistogramFilterType::New();
  this->BackgroundHistogramFilter = SampleToHistogramFilterType::New();

}

template <typename TImageType>
void ImageGraphCut<TImageType>::CutGraph()
{
  // Compute max-flow
  this->Graph->maxflow();

  // Setup the values of the output (mask) image
  GrayscalePixelType sinkPixel;
  sinkPixel[0] = 0;

  GrayscalePixelType sourcePixel;
  sourcePixel[0] = 255;

  // Iterate over the node image, querying the Kolmorogov graph object for the association of each pixel and storing them as the output mask
  itk::ImageRegionConstIterator<NodeImageType> nodeImageIterator(this->NodeImage, this->NodeImage->GetLargestPossibleRegion());
  nodeImageIterator.GoToBegin();

  while(!nodeImageIterator.IsAtEnd())
    {
    if(this->Graph->what_segment(nodeImageIterator.Get()) == GraphType::SOURCE)
      {
      this->SegmentMask->SetPixel(nodeImageIterator.GetIndex(), sourcePixel);
      }
    else if(this->Graph->what_segment(nodeImageIterator.Get()) == GraphType::SINK)
      {
      this->SegmentMask->SetPixel(nodeImageIterator.GetIndex(), sinkPixel);
      }
    ++nodeImageIterator;
    }

  delete this->Graph;
}

template <typename TImageType>
void ImageGraphCut<TImageType>::PerformSegmentation()
{
  // This function performs some initializations and then creates and cuts the graph

  // Ensure at least one pixel has been specified for both the foreground and background
  if((this->Sources.size() <= 0) || (this->Sinks.size() <= 0))
    {
    //std::cout << "At least one source (foreground) pixel and one sink (background) pixel must be specified!" << std::endl;
    QMessageBox msgBox;
    msgBox.setText("At least one source (foreground) pixel and one sink (background) pixel must be specified!");
    msgBox.exec();
    return;
    }

  // Blank the NodeImage
  itk::ImageRegionIterator<NodeImageType> nodeImageIterator(this->NodeImage, this->NodeImage->GetLargestPossibleRegion());
  nodeImageIterator.GoToBegin();

  while(!nodeImageIterator.IsAtEnd())
    {
    nodeImageIterator.Set(NULL);
    ++nodeImageIterator;
    }

  // Blank the output image
  itk::ImageRegionIterator<GrayscaleImageType> segmentMaskImageIterator(this->SegmentMask, this->SegmentMask->GetLargestPossibleRegion());
  segmentMaskImageIterator.GoToBegin();

  MaskImageType::PixelType empty;
  empty[0] = 0;

  while(!segmentMaskImageIterator.IsAtEnd())
    {
    segmentMaskImageIterator.Set(empty);
    ++segmentMaskImageIterator;
    }

  this->CreateGraph();
  this->CutGraph();
}

template <typename TImageType>
void ImageGraphCut<TImageType>::CreateSamples()
{
  // This function creates ITK samples from the scribbled pixels and then computes the foreground and background histograms

  // We want the histogram bins to take values from 0 to 255 in all dimensions
  HistogramType::MeasurementVectorType binMinimum(TImageType::PixelType::GetNumberOfComponents());
  HistogramType::MeasurementVectorType binMaximum(TImageType::PixelType::GetNumberOfComponents());
  for(unsigned int i = 0; i < TImageType::PixelType::GetNumberOfComponents(); i++)
    {
    binMinimum[i] = 0;
    binMaximum[i] = 255;
    }

  // Setup the histogram size
  typename SampleToHistogramFilterType::HistogramSizeType histogramSize(TImageType::PixelType::GetNumberOfComponents());
  histogramSize.Fill(this->NumberOfHistogramBins);

  // Create foreground samples and histogram
  this->ForegroundSample->Clear();
  for(unsigned int i = 0; i < this->Sources.size(); i++)
    {
    this->ForegroundSample->PushBack(this->Image->GetPixel(this->Sources[i]));
    }

  this->ForegroundHistogramFilter->SetHistogramSize(histogramSize);
  this->ForegroundHistogramFilter->SetHistogramBinMinimum(binMinimum);
  this->ForegroundHistogramFilter->SetHistogramBinMaximum(binMaximum);
  this->ForegroundHistogramFilter->SetAutoMinimumMaximum(false);
  this->ForegroundHistogramFilter->SetInput(this->ForegroundSample);
  this->ForegroundHistogramFilter->Modified();
  this->ForegroundHistogramFilter->Update();

  this->ForegroundHistogram = this->ForegroundHistogramFilter->GetOutput();

  // Create background samples and histogram
  this->BackgroundSample->Clear();
  for(unsigned int i = 0; i < this->Sinks.size(); i++)
    {
    this->BackgroundSample->PushBack(this->Image->GetPixel(this->Sinks[i]));
    }

  this->BackgroundHistogramFilter->SetHistogramSize(histogramSize);
  this->BackgroundHistogramFilter->SetHistogramBinMinimum(binMinimum);
  this->BackgroundHistogramFilter->SetHistogramBinMaximum(binMaximum);
  this->BackgroundHistogramFilter->SetAutoMinimumMaximum(false);
  this->BackgroundHistogramFilter->SetInput(this->BackgroundSample);
  this->BackgroundHistogramFilter->Modified();
  this->BackgroundHistogramFilter->Update();

  this->BackgroundHistogram = BackgroundHistogramFilter->GetOutput();

}

template <typename TImageType>
void ImageGraphCut<TImageType>::CreateGraph()
{
  // Form the graph
  this->Graph = new GraphType;

  // Add all of the nodes to the graph and store their IDs in a "node image"
  itk::ImageRegionIterator<NodeImageType> nodeImageIterator(this->NodeImage, this->NodeImage->GetLargestPossibleRegion());
  nodeImageIterator.GoToBegin();

  while(!nodeImageIterator.IsAtEnd())
    {
    nodeImageIterator.Set(this->Graph->add_node());
    ++nodeImageIterator;
    }

  // Estimate the "camera noise"
  double sigma = this->ComputeNoise();

  ////////// Create n-edges and set n-edge weights (links between image nodes) //////////

  // We are only using a 4-connected structure, so the kernel (iteration neighborhood) must only be 3x3 (specified by a radius of 1)
  itk::Size<2> radius;
  radius[0] = 1;
  radius[1] = 1;

  typedef itk::ShapedNeighborhoodIterator<TImageType> IteratorType;

  // Traverse the image adding an edge between the current pixel and the pixel below it and the current pixel and the pixel to the right of it.
  // This prevents duplicate edges (i.e. we cannot add an edge to all 4-connected neighbors of every pixel or almost every edge would be duplicated.
  std::vector<typename IteratorType::OffsetType> neighbors;
  typename IteratorType::OffsetType bottom = {{0,1}};
  neighbors.push_back(bottom);
  typename IteratorType::OffsetType right = {{1,0}};
  neighbors.push_back(right);

  typename IteratorType::OffsetType center = {{0,0}};

  IteratorType iterator(radius, this->Image, this->Image->GetLargestPossibleRegion());
  iterator.ClearActiveList();
  iterator.ActivateOffset(bottom);
  iterator.ActivateOffset(right);
  iterator.ActivateOffset(center);

  for(iterator.GoToBegin(); !iterator.IsAtEnd(); ++iterator)
    {
    typename TImageType::PixelType centerPixel = iterator.GetPixel(center);

    for(unsigned int i = 0; i < neighbors.size(); i++)
      {
      bool valid;
      iterator.GetPixel(neighbors[i], valid);

      // If the current neighbor is outside the image, skip it
      if(!valid)
        {
        continue;
        }
      typename TImageType::PixelType neighborPixel = iterator.GetPixel(neighbors[i]);

      // Compute the Euclidean distance between the pixel intensities
      float pixelDifference = PixelDifference(centerPixel, neighborPixel);

      // Compute the edge weight
      float weight = exp(-pow(pixelDifference,2)/(2.0*sigma*sigma));
      assert(weight >= 0);

      // Add the edge to the graph
      void* node1 = this->NodeImage->GetPixel(iterator.GetIndex(center));
      void* node2 = this->NodeImage->GetPixel(iterator.GetIndex(neighbors[i]));
      this->Graph->add_edge(node1, node2, weight, weight);
      }
    }

  ////////// Add t-edges and set t-edge weights (links from image nodes to virtual background and virtual foreground node) //////////

  // Compute the histograms of the selected foreground and background pixels
  CreateSamples();

  itk::ImageRegionIterator<TImageType> imageIterator(this->Image, this->Image->GetLargestPossibleRegion());
  itk::ImageRegionIterator<NodeImageType> nodeIterator(this->NodeImage, this->NodeImage->GetLargestPossibleRegion());
  imageIterator.GoToBegin();
  nodeIterator.GoToBegin();

  // Since the t-weight function takes the log of the histogram value,
  // we must handle bins with frequency = 0 specially (because log(0) = -inf)
  // For empty histogram bins we use tinyValue instead of 0.
  float tinyValue = 1e-10;

  while(!imageIterator.IsAtEnd())
    {
    typename TImageType::PixelType pixel = imageIterator.Get();

    HistogramType::MeasurementVectorType measurementVector(pixel.Size());
    for(unsigned int i = 0; i < pixel.Size(); i++)
      {
      measurementVector[i] = pixel[i];
      }

    float sinkHistogramValue = this->BackgroundHistogram->GetFrequency(this->BackgroundHistogram->GetIndex(measurementVector));
    float sourceHistogramValue = this->ForegroundHistogram->GetFrequency(this->ForegroundHistogram->GetIndex(measurementVector));

    // Conver the histogram value/frequency to make it as if it came from a normalized histogram
    sinkHistogramValue /= this->BackgroundHistogram->GetTotalFrequency();
    sourceHistogramValue /= this->ForegroundHistogram->GetTotalFrequency();

    if(sinkHistogramValue <= 0)
      {
      sinkHistogramValue = tinyValue;
      }
    if(sourceHistogramValue <= 0)
      {
      sourceHistogramValue = tinyValue;
      }

    // Add the edge to the graph and set its weight
    this->Graph->add_tweights(nodeIterator.Get(),
                              -this->Lambda*log(sinkHistogramValue),
                              -this->Lambda*log(sourceHistogramValue)); // log() is the natural log
    ++imageIterator;
    ++nodeIterator;
    }

  // Set very high source weights for the pixels which were selected as foreground by the user
  for(unsigned int i = 0; i < this->Sources.size(); i++)
    {
    this->Graph->add_tweights(this->NodeImage->GetPixel(this->Sources[i]),this->Lambda * std::numeric_limits<float>::max(),0);
    }

  // Set very high sink weights for the pixels which were selected as background by the user
  for(unsigned int i = 0; i < this->Sinks.size(); i++)
    {
    this->Graph->add_tweights(this->NodeImage->GetPixel(this->Sinks[i]),0,this->Lambda * std::numeric_limits<float>::max());
    }
}

template <typename TImageType>
template <typename TPixelType>
float ImageGraphCut<TImageType>::PixelDifference(TPixelType a, TPixelType b)
{
  // Compute the Euclidean distance between N dimensional pixels
  float difference = 0;
  for(unsigned int i = 0; i < TPixelType::GetNumberOfComponents(); i++)
    {
    difference += pow(a[i] - b[i],2);
    }
  return sqrt(difference);
}

template <typename TImageType>
double ImageGraphCut<TImageType>::ComputeNoise()
{
  // Compute an estimate of the "camera noise". This is used in the N-weight function.

  // Since we use a 4-connected neighborhood, the kernel must be 3x3 (a rectangular radius of 1 creates a kernel side length of 3)
  itk::Size<2> radius;
  radius[0] = 1;
  radius[1] = 1;

  typedef itk::ShapedNeighborhoodIterator<TImageType> IteratorType;

  std::vector<typename IteratorType::OffsetType> neighbors;
  typename IteratorType::OffsetType bottom = {{0,1}};
  neighbors.push_back(bottom);
  typename IteratorType::OffsetType right = {{1,0}};
  neighbors.push_back(right);

  typename IteratorType::OffsetType center = {{0,0}};

  IteratorType iterator(radius, this->Image, this->Image->GetLargestPossibleRegion());
  iterator.ClearActiveList();
  iterator.ActivateOffset(bottom);
  iterator.ActivateOffset(right);
  iterator.ActivateOffset(center);

  double sigma = 0.0;
  int numberOfEdges = 0;

  // Traverse the image collecting the differences between neighboring pixel intensities
  for(iterator.GoToBegin(); !iterator.IsAtEnd(); ++iterator)
    {
    typename TImageType::PixelType centerPixel = iterator.GetPixel(center);

    for(unsigned int i = 0; i < neighbors.size(); i++)
      {
      bool valid;
      iterator.GetPixel(neighbors[i], valid);
      if(!valid)
        {
        continue;
        }
      typename TImageType::PixelType neighborPixel = iterator.GetPixel(neighbors[i]);

      float colorDifference = PixelDifference(centerPixel, neighborPixel);
      sigma += colorDifference;
      numberOfEdges++;
      }

    }

  // Normalize
  sigma /= static_cast<double>(numberOfEdges);

  return sigma;
}

// Explicit template instantiations
template class ImageGraphCut<ColorImageType>;
template class ImageGraphCut<GrayscaleImageType>;