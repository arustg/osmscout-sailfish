#ifndef OSMSCOUT_IMPORT_GENNUMERICINDEX_H
#define OSMSCOUT_IMPORT_GENNUMERICINDEX_H

/*
  This source is part of the libosmscout library
  Copyright (C) 2009  Tim Teulings

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <vector>

#include <osmscout/Progress.h>

#include <osmscout/Util.h>

#include <osmscout/util/Cache.h>
#include <osmscout/util/FileScanner.h>
#include <osmscout/util/FileWriter.h>
#include <osmscout/util/String.h>

#include <osmscout/import/Import.h>

namespace osmscout {

  template <class N,class T>
  class NumericIndexGenerator : public ImportModule
  {
  private:
    std::string description;
    std::string datafile;
    std::string indexfile;

  public:
    NumericIndexGenerator(const std::string& description,
                          const std::string& datafile,
                          const std::string& indexfile);
    virtual ~NumericIndexGenerator();

    std::string GetDescription() const;
    bool Import(const ImportParameter& parameter,
                Progress& progress,
                const TypeConfig& typeConfig);
  };

  template <class N,class T>
  NumericIndexGenerator<N,T>::NumericIndexGenerator(const std::string& description,
                                                    const std::string& datafile,
                                                    const std::string& indexfile)
   : description(description),
     datafile(datafile),
     indexfile(indexfile)
  {
    // no code
  }

  template <class N,class T>
  NumericIndexGenerator<N,T>::~NumericIndexGenerator()
  {
    // no code
  }

  template <class N,class T>
  std::string NumericIndexGenerator<N,T>::GetDescription() const
  {
    return description;
  }

  template <class N,class T>
  bool NumericIndexGenerator<N,T>::Import(const ImportParameter& parameter,
                                          Progress& progress,
                                          const TypeConfig& typeConfig)
  {
    FileScanner             scanner;
    FileWriter              writer;

    uint32_t                dataCount;

    std::vector<Id>         startingIds;
    std::vector<FileOffset> pageStarts;

    std::vector<uint32_t>   indexPageCounts;

    FileOffset              levelsOffset;
    FileOffset              lastLevelPageStartOffset;

    FileOffset              indexPageCountsOffset;
    uint32_t                pageSize=parameter.GetNumericIndexPageSize();

    //
    // Writing index file
    //

    progress.SetAction(std::string("Generating '")+indexfile+"'");

    if (!writer.Open(indexfile)) {
      progress.Error(std::string("Cannot create '")+indexfile+"'");
      return false;
    }

    if (!scanner.Open(datafile)) {
      progress.Error(std::string("Cannot open '")+datafile+"'");
      return false;
    }

    if (!scanner.Read(dataCount)) {
      progress.Error("Error while reading number of data entries in file");
      return false;
    }

    writer.WriteNumber(pageSize);       // Size of one index page in bytes
    writer.WriteNumber(dataCount);      // Number of entries in data file

    writer.GetPos(levelsOffset);
    writer.Write((FileOffset)0);        // Number of levels

    writer.GetPos(lastLevelPageStartOffset);
    writer.Write((FileOffset)0);        // Write the starting position of the last page

    writer.GetPos(indexPageCountsOffset);
    writer.Write((FileOffset)0);        // Write the starting position of list of sizes of each index level

    writer.FlushCurrentBlockWithZeros(pageSize);

    Id         lastId=0;
    FileOffset lastPos=0;

    progress.Info(std::string("Writing level ")+NumberToString(1)+" ("+NumberToString(dataCount)+" entries)");

    uint32_t currentPageSize=0;

    for (uint32_t d=0; d<dataCount; d++) {
      progress.SetProgress(d,dataCount);

      FileOffset readPos;

      scanner.GetPos(readPos);

      T data;

      if (!data.Read(scanner)) {
        progress.Error(std::string("Error while reading data entry ")+
                       NumberToString(d+1)+" of "+
                       NumberToString(dataCount)+
                       " in file '"+
                       scanner.GetFilename()+"'");
        return false;
      }

      if (currentPageSize>0) {
        char   b1[5];
        char   b2[5];
        size_t b1size;
        size_t b2size;

        EncodeNumber(data.GetId()-lastId,5,b1,b1size);
        EncodeNumber(readPos-lastPos,5,b2,b2size);

        if (currentPageSize+b1size+b2size>pageSize) {
          // Next entry does not fit, fill rest of index page with zeros
          writer.FlushCurrentBlockWithZeros(pageSize);

          currentPageSize=0;
        }
        else {
          writer.Write(b1,b1size);
          writer.Write(b2,b2size);

          currentPageSize+=b1size+b2size;
        }
      }

      if (currentPageSize==0) {
        FileOffset writePos;

        writer.GetPos(writePos);

        startingIds.push_back(data.GetId());
        pageStarts.push_back(writePos);

        writer.WriteNumber(data.GetId());
        writer.WriteNumber(readPos);

        writer.GetPos(writePos);
        currentPageSize=writePos%pageSize;
      }

      lastId=data.GetId();
      lastPos=readPos;
    }

    writer.FlushCurrentBlockWithZeros(pageSize);
    indexPageCounts.push_back(pageStarts.size());

    while (pageStarts.size()>1) {
      std::vector<Id>         si(startingIds);
      std::vector<FileOffset> po(pageStarts);

      startingIds.clear();
      pageStarts.clear();

      progress.Info(std::string("Writing level ")+NumberToString(indexPageCounts.size()+1)+" ("+NumberToString(si.size())+" entries)");

      uint32_t currentPageSize=0;

      for (size_t i=0; i<si.size(); i++) {

        if (currentPageSize>0) {
          char   b1[5];
          char   b2[5];
          size_t b1size;
          size_t b2size;

          EncodeNumber((si[i]-si[i-1]),5,b1,b1size);
          EncodeNumber(po[i]-po[i-1],5,b2,b2size);

          if (currentPageSize+b1size+b2size>pageSize) {
            // Fill rest of first index page with zeros
            writer.FlushCurrentBlockWithZeros(pageSize);

            currentPageSize=0;
          }
          else {
            writer.Write(b1,b1size);
            writer.Write(b2,b2size);

            currentPageSize+=b1size+b2size;
          }
        }

        if (currentPageSize==0) {
          FileOffset writePos;

          writer.GetPos(writePos);

          startingIds.push_back(si[i]);
          pageStarts.push_back(writePos);

          writer.WriteNumber(si[i]);
          writer.WriteNumber(po[i]);

          writer.GetPos(writePos);
          currentPageSize=writePos%pageSize;
        }
      }

      writer.FlushCurrentBlockWithZeros(pageSize);
      indexPageCounts.push_back(pageStarts.size());
    }

    // If we have data to index, we should have at least one root level index page
    if (dataCount>0) {
      assert(pageStarts.size()==1);

      FileOffset indexPageCountsPos;

      writer.GetPos(indexPageCountsPos);

      writer.SetPos(levelsOffset);
      writer.Write((uint32_t)indexPageCounts.size());

      writer.SetPos(lastLevelPageStartOffset);
      writer.Write(pageStarts[0]);

      writer.SetPos(indexPageCountsOffset);
      writer.Write(indexPageCountsPos);

      writer.SetPos(indexPageCountsPos);
    }

    progress.Info(std::string("Index for ")+NumberToString(dataCount)+" data elements will be stored in "+NumberToString(indexPageCounts.size())+ " levels");
    for (size_t i=0; i<indexPageCounts.size(); i++) {
      progress.Info(std::string("Page count for level ")+NumberToString(indexPageCounts.size()-i)+" is "+NumberToString(indexPageCounts[i]));
      writer.WriteNumber(indexPageCounts[i]);
    }

    return !scanner.HasError() &&
           scanner.Close() &&
           !writer.HasError() &&
           writer.Close();
  }
}

#endif
