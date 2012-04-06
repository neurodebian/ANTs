/*=========================================================================

  Program:   Advanced Normalization Tools
  Module:    $RCSfile: ConvertToJpg.cxx,v $
  Language:  C++
  Date:      $Date: 2008/11/15 23:46:06 $
  Version:   $Revision: 1.19 $

  Copyright (c) ConsortiumOfANTS. All rights reserved.
  See accompanying COPYING.txt or
 http://sourceforge.net/projects/advants/files/ANTS/ANTSCopyright.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include "antscout.hxx"

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <limits.h>
#include "itkImage.h"
#include "itkImageFileWriter.h"
#include "itkImageFileReader.h"
#include "itkCastImageFilter.h"
#include "itkRescaleIntensityImageFilter.h"

namespace ants
{


template <unsigned int ImageDimension, class TPIXELTYPE>
int ConvertType(int argc, char *argv[], double MINVAL, double MAXVAL)
{

  typedef  TPIXELTYPE                                outPixelType;
  typedef  float                                     floatPixelType;
  typedef  float                                     inPixelType;
  typedef itk::Image<inPixelType, ImageDimension>    ImageType;
  typedef itk::Image<floatPixelType, ImageDimension> IntermediateType;
  typedef itk::Image<outPixelType, ImageDimension>   OutImageType;
  typedef itk::ImageFileReader<ImageType>            readertype;
  typedef itk::ImageFileWriter<OutImageType>         writertype;

  typename readertype::Pointer reader = readertype::New();
  if( argc < 2 )
    {
    antscout << "Missing input filename" << std::endl;
    throw;
    }
  reader->SetFileName(argv[1]);
  reader->Update();
  antscout << " Updated reader " << std::endl;

  typedef itk::CastImageFilter<ImageType, IntermediateType> castertype;
  typename   castertype::Pointer caster = castertype::New();
  caster->SetInput(reader->GetOutput() );
  caster->Update();

  // Rescale the image intensities so that they fall between 0 and 255
  typedef itk::RescaleIntensityImageFilter<IntermediateType, IntermediateType> FilterType;
  typename   FilterType::Pointer fixedrescalefilter = FilterType::New();
  fixedrescalefilter->SetInput(caster->GetOutput() );
  const double desiredMinimum =  MINVAL;
  double       desiredMaximum =  MAXVAL;
  fixedrescalefilter->SetOutputMinimum( desiredMinimum );
  fixedrescalefilter->SetOutputMaximum( desiredMaximum );
  fixedrescalefilter->UpdateLargestPossibleRegion();

  typedef itk::CastImageFilter<IntermediateType, OutImageType> castertype2;
  typename castertype2::Pointer caster2 = castertype2::New();
  caster2->SetInput(fixedrescalefilter->GetOutput() );
  caster2->Update();

  typename   OutImageType::Pointer outim = caster2->GetOutput();
  typename   OutImageType::SpacingType spc = outim->GetSpacing();
  outim->SetSpacing(spc);
  antscout << " Dire in " << reader->GetOutput()->GetDirection() << std::endl;
  antscout << " Dire out " << outim->GetDirection() << std::endl;
  typename   writertype::Pointer writer = writertype::New();
  if( argc < 3 )
    {
    antscout << "Missing output filename" << std::endl;
    throw;
    }
  writer->SetFileName(argv[2]);
  writer->SetInput(outim);
  writer->Update();
  writer->Write();

  return 0;

}

// entry point for the library; parameter 'args' is equivalent to 'argv' in (argc,argv) of commandline parameters to 'main()'
int ConvertImagePixelType( std::vector<std::string> args , std::ostream* out_stream = NULL )
{
  // put the arguments coming in as 'args' into standard (argc,argv) format;
  // 'args' doesn't have the command name as first, argument, so add it manually;
  // 'args' may have adjacent arguments concatenated into one argument,
  // which the parser should handle
  args.insert( args.begin() , "ConvertImagePixelType" ) ;

  int argc = args.size() ;
  char** argv = new char*[args.size()+1] ;
  for( unsigned int i = 0 ; i < args.size() ; ++i )
    {
      // allocate space for the string plus a null character
      argv[i] = new char[args[i].length()+1] ;
      std::strncpy( argv[i] , args[i].c_str() , args[i].length() ) ;
      // place the null character in the end
      argv[i][args[i].length()] = '\0' ;
    }
  argv[argc] = 0 ;
  // class to automatically cleanup argv upon destruction
  class Cleanup_argv
  {
  public:
    Cleanup_argv( char** argv_ , int argc_plus_one_ ) : argv( argv_ ) , argc_plus_one( argc_plus_one_ )
    {}
    ~Cleanup_argv()
    {
      for( unsigned int i = 0 ; i < argc_plus_one ; ++i )
	{
	  delete[] argv[i] ;
	}
      delete[] argv ;
    }
  private:
    char** argv ;
    unsigned int argc_plus_one ;
  } ;
  Cleanup_argv cleanup_argv( argv , argc+1 ) ;

  antscout.set_ostream( out_stream ) ;

  if( argc < 3 )
    {
    antscout << "Usage:   " << argv[0] << " infile.nii out.ext TYPE-OPTION " << std::endl;
    antscout << " ext is the extension you want, e.g. tif.  " << std::endl;
    antscout << " TYPE-OPTION  :  TYPE " << std::endl;
    antscout << "  0  :  char   " << std::endl;
    antscout << "  1  :  unsigned char   " << std::endl;
    antscout << "  2  :  short   " << std::endl;
    antscout << "  3  :  unsigned short   " << std::endl;
    antscout << "  4  :  int   " << std::endl;
    antscout << "  5  :  unsigned int   " << std::endl;
    antscout
    << " Note that some pixel types are not supported by some image formats. e.g.  int is not supported by jpg. "
    << std::endl;
    antscout
    << " You can easily extend this for other pixel types with a few lines of code and adding usage info. "
    << std::endl;
    antscout
    <<
    " The image intensity will be scaled to the dynamic range of the pixel type.  E.g. uchar => 0  (min), 255 (max). "
    << std::endl;
    return 1;
    }
  unsigned int typeoption = 0;
  if( argc > 3 )
    {
    typeoption = atoi(argv[3]);
    }
  // Get the image dimension
  std::string               fn = std::string(argv[1]);
  itk::ImageIOBase::Pointer imageIO =
    itk::ImageIOFactory::CreateImageIO(
      fn.c_str(), itk::ImageIOFactory::ReadMode);
  imageIO->SetFileName(fn.c_str() );
  imageIO->ReadImageInformation();

  if( typeoption == 0 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, char>(argc, argv, SCHAR_MIN, SCHAR_MAX );
        break;
      case 3:
        ConvertType<3, char>(argc, argv,  SCHAR_MIN, SCHAR_MAX );
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }
  else if( typeoption == 1 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, unsigned char>(argc, argv,  0, UCHAR_MAX );
        break;
      case 3:
        ConvertType<3, unsigned char>(argc, argv,  0, UCHAR_MAX );
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }
  else if( typeoption == 2 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, short>(argc, argv,  SHRT_MIN, SHRT_MAX);
        break;
      case 3:
        ConvertType<3, short>(argc, argv,  SHRT_MIN, SHRT_MAX);
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }
  else if( typeoption == 3 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, unsigned short>(argc, argv,  0, USHRT_MAX );
        break;
      case 3:
        ConvertType<3, unsigned short>(argc, argv,  0, USHRT_MAX );
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }
  else if( typeoption == 4 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, int>(argc, argv,  INT_MIN, INT_MAX);
        break;
      case 3:
        ConvertType<3, int>(argc, argv,  INT_MIN, INT_MAX);
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }
  else if( typeoption == 5 )
    {
    switch( imageIO->GetNumberOfDimensions() )
      {
      case 2:
        ConvertType<2, unsigned int>(argc, argv,  0, UINT_MAX );
        break;
      case 3:
        ConvertType<3, unsigned int>(argc, argv,  0, UINT_MAX );
        break;
      default:
        antscout << "Unsupported dimension" << std::endl;
        throw std::exception();
      }
    }

  return 0;
}



} // namespace ants


