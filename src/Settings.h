#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "LittleFS.h"
#include <list>

namespace Settings {
   template <typename T>
   bool save(const T &data, uint32_t version = 0, const char *file = "/settings.dat") {
       Serial.println("[Settings] saved");
      File f = LittleFS.open(file, "w");
      f.write((uint8_t*)&version, sizeof(uint32_t));
      f.write((uint8_t*)&data, sizeof(T));
      f.close();

      return true;
   }

   template <typename T>
   bool load(const T &data, uint32_t version = 0, const char *file = "/settings.dat") {
      if(!LittleFS.exists(file)) {
         Serial.println("[Settings] file not found");
         return false;
      }

      size_t expectedSize = sizeof(T) + sizeof(uint32_t);

      File f = LittleFS.open(file, "r");
      
      if(f.size() != expectedSize) {
         Serial.printf("[Settings] file size mismatch (%d vs %d)", f.size(), expectedSize);
         return false;
      }

      uint32_t fileVersion;
      f.read((uint8_t *)&fileVersion, sizeof(uint32_t));

      if(fileVersion != version) {
         Serial.printf("[Settings] file version mismatch (%d vs %d)", fileVersion, version);
         return false;
      }

      f.read((uint8_t *)&data, sizeof(T));

      f.close();

      return true;
   }

   template <typename T>
   bool saveList(const std::list<T> &data, uint32_t version = 0, const char *file = "/list.dat") {
      File f = LittleFS.open(file, "w");
      f.write((uint8_t*)&version, sizeof(uint32_t));
      size_t length = data.size();
      f.write((uint8_t*)&length, sizeof(size_t));
      for(auto &item : data) {
         f.write((uint8_t*)&item, sizeof(T));
      }
      f.close();

      return true;
   }

   template <typename T>
   bool loadList(std::list<T> &data, uint32_t version = 0, const char *file = "/list.dat") {
      if(!LittleFS.exists(file)) {
         Serial.println("[Settings] file not found");
         return false;
      }

      File f = LittleFS.open(file, "r");
      
      if((f.size() - sizeof(uint32_t) - sizeof(size_t)) % sizeof(T) != 0) {
         Serial.printf("[Settings] file size mismatch (file %d, item %d)", f.size(), sizeof(T));
         return false;
      }

      uint32_t fileVersion;
      f.read((uint8_t *)&fileVersion, sizeof(uint32_t));

      if(fileVersion != version) {
         Serial.printf("[Settings] file version mismatch (%d vs %d)", fileVersion, version);
         return false;
      }

      size_t length;
      f.read((uint8_t *)&length, sizeof(size_t));

      for(size_t idx = 0; idx < length; idx++) {
         T item;
         f.read((uint8_t*)&item, sizeof(T));
         data.push_back(item);
      }

      f.close();

      return true;
   }
}

#endif