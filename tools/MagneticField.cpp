/**
 * \file MagneticField.cpp
 * \brief Command line utility for evaluating magnetic fields
 *
 * Copyright (c) Charles Karney (2011, 2012) <charles@karney.com> and licensed
 * under the MIT/X11 License.  For more information, see
 * http://geographiclib.sourceforge.net/
 *
 * Compile and link with
 *   g++ -g -O3 -I../include -I../man -o MagneticField \
 *       MagneticField.cpp \
 *       ../src/CircularEngine.cpp \
 *       ../src/DMS.cpp \
 *       ../src/Geocentric.cpp \
 *       ../src/MagneticCircle.cpp \
 *       ../src/MagneticModel.cpp \
 *       ../src/SphericalEngine.cpp \
 *       ../src/Utility.cpp
 *
 * See the <a href="MagneticField.1.html">man page</a> for usage
 * information.
 **********************************************************************/

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <GeographicLib/MagneticModel.hpp>
#include <GeographicLib/MagneticCircle.hpp>
#include <GeographicLib/DMS.hpp>
#include <GeographicLib/Utility.hpp>

#include "MagneticField.usage"

int main(int argc, char* argv[]) {
  try {
    using namespace GeographicLib;
    typedef Math::real real;
    bool verbose = false;
    std::string dir;
    std::string model = MagneticModel::DefaultMagneticName();
    std::string istring, ifile, ofile, cdelim;
    char lsep = ';';
    real time = 0, lat = 0, h = 0;
    bool timeset = false, circle = false, rate = false;
    real hguard = 500000, tguard = 50;
    int prec = 1;

    for (int m = 1; m < argc; ++m) {
      std::string arg(argv[m]);
      if (arg == "-n") {
        if (++m == argc) return usage(1, true);
        model = argv[m];
      } else if (arg == "-d") {
        if (++m == argc) return usage(1, true);
        dir = argv[m];
      } else if (arg == "-t") {
        if (++m == argc) return usage(1, true);
        try {
          time = Utility::fractionalyear<real>(std::string(argv[m]));
          timeset = true;
          circle = false;
        }
        catch (const std::exception& e) {
          std::cerr << "Error decoding argument of " << arg << ": "
                    << e.what() << "\n";
          return 1;
        }
      } else if (arg == "-c") {
        if (m + 3 >= argc) return usage(1, true);
        try {
          time = Utility::fractionalyear<real>(std::string(argv[++m]));
          DMS::flag ind;
          lat = DMS::Decode(std::string(argv[++m]), ind);
          if (ind == DMS::LONGITUDE)
            throw GeographicErr("Bad hemisphere letter on latitude");
          if (!(std::abs(lat) <= 90))
            throw GeographicErr("Latitude not in [-90d, 90d]");
          h = Utility::num<real>(std::string(argv[++m]));
          timeset = false;
          circle = true;
        }
        catch (const std::exception& e) {
          std::cerr << "Error decoding argument of " << arg << ": "
                    << e.what() << "\n";
          return 1;
        }
      } else if (arg == "-r")
        rate = !rate;
      else if (arg == "-p") {
        if (++m == argc) return usage(1, true);
        try {
          prec = Utility::num<int>(std::string(argv[m]));
        }
        catch (const std::exception&) {
          std::cerr << "Precision " << argv[m] << " is not a number\n";
          return 1;
        }
      } else if (arg == "-T") {
        if (++m == argc) return usage(1, true);
        try {
          tguard = Utility::num<real>(std::string(argv[m]));
        }
        catch (const std::exception& e) {
          std::cerr << "Error decoding argument of " << arg << ": "
                    << e.what() << "\n";
          return 1;
        }
      } else if (arg == "-H") {
        if (++m == argc) return usage(1, true);
        try {
          hguard = Utility::num<real>(std::string(argv[m]));
        }
        catch (const std::exception& e) {
          std::cerr << "Error decoding argument of " << arg << ": "
                    << e.what() << "\n";
          return 1;
        }
      } else if (arg == "-v")
        verbose = true;
      else if (arg == "--input-string") {
        if (++m == argc) return usage(1, true);
        istring = argv[m];
      } else if (arg == "--input-file") {
        if (++m == argc) return usage(1, true);
        ifile = argv[m];
      } else if (arg == "--output-file") {
        if (++m == argc) return usage(1, true);
        ofile = argv[m];
      } else if (arg == "--line-separator") {
        if (++m == argc) return usage(1, true);
        if (std::string(argv[m]).size() != 1) {
          std::cerr << "Line separator must be a single character\n";
          return 1;
        }
        lsep = argv[m][0];
      } else if (arg == "--comment-delimiter") {
        if (++m == argc) return usage(1, true);
        cdelim = argv[m];
      } else if (arg == "--version") {
        std::cout
          << argv[0]
          << ": $Id: cd55a73582dee908c12a23bee33362e7607268af $\n"
          << "GeographicLib version " << GEOGRAPHICLIB_VERSION_STRING << "\n";
        return 0;
      } else {
        int retval = usage(!(arg == "-h" || arg == "--help"), arg != "--help");
        if (arg == "-h")
          std::cout<< "\nDefault magnetic path = \""
                   << MagneticModel::DefaultMagneticPath()
                   << "\"\nDefault magnetic name = \""
                   << MagneticModel::DefaultMagneticName()
                   << "\"\n";
        return retval;
      }
    }

    if (!ifile.empty() && !istring.empty()) {
      std::cerr << "Cannot specify --input-string and --input-file together\n";
      return 1;
    }
    if (ifile == "-") ifile.clear();
    std::ifstream infile;
    std::istringstream instring;
    if (!ifile.empty()) {
      infile.open(ifile.c_str());
      if (!infile.is_open()) {
        std::cerr << "Cannot open " << ifile << " for reading\n";
        return 1;
      }
    } else if (!istring.empty()) {
      std::string::size_type m = 0;
      while (true) {
        m = istring.find(lsep, m);
        if (m == std::string::npos)
          break;
        istring[m] = '\n';
      }
      instring.str(istring);
    }
    std::istream* input = !ifile.empty() ? &infile :
      (!istring.empty() ? &instring : &std::cin);

    std::ofstream outfile;
    if (ofile == "-") ofile.clear();
    if (!ofile.empty()) {
      outfile.open(ofile.c_str());
      if (!outfile.is_open()) {
        std::cerr << "Cannot open " << ofile << " for writing\n";
        return 1;
      }
    }
    std::ostream* output = !ofile.empty() ? &outfile : &std::cout;

    tguard = std::max(real(0), tguard);
    hguard = std::max(real(0), hguard);
    prec = std::min(10, std::max(0, prec));
    int retval = 0;
    try {
      const MagneticModel m(model, dir);
      if ((timeset || circle)
          && (!Math::isfinite<real>(time) ||
              time < m.MinTime() - tguard ||
              time > m.MaxTime() + tguard))
        throw GeographicErr("Time " + Utility::str(time) +
                            " too far outside allowed range [" +
                            Utility::str(m.MinTime()) + "," +
                            Utility::str(m.MaxTime()) + "]");
      if (circle
          && (!Math::isfinite<real>(h) ||
              h < m.MinHeight() - hguard ||
              h > m.MaxHeight() + hguard))
        throw GeographicErr("Height " + Utility::str(h/1000) +
                            "km too far outside allowed range [" +
                            Utility::str(m.MinHeight()/1000) + "km," +
                            Utility::str(m.MaxHeight()/1000) + "km]");
      if (verbose) {
        std::cerr << "Magnetic file: " << m.MagneticFile()      << "\n"
                  << "Name: "          << m.MagneticModelName() << "\n"
                  << "Description: "   << m.Description()       << "\n"
                  << "Date & Time: "   << m.DateTime()          << "\n"
                  << "Time range: ["
                  << m.MinTime() << ","
                  << m.MaxTime() << "]\n"
                  << "Height range: ["
                  << m.MinHeight()/1000 << "km,"
                  << m.MaxHeight()/1000 << "km]\n";
      }
      if ((timeset || circle) && (time < m.MinTime() || time > m.MaxTime()))
        std::cerr << "WARNING: Time " << time
                  << " outside allowed range ["
                  << m.MinTime() << "," << m.MaxTime() << "]\n";
      if (circle && (h < m.MinHeight() || h > m.MaxHeight()))
        std::cerr << "WARNING: Height " << h/1000
                  << "km outside allowed range ["
                  << m.MinHeight()/1000 << "km,"
                  << m.MaxHeight()/1000 << "km]\n";
      const MagneticCircle c(circle ? m.Circle(time, lat, h) :
                             MagneticCircle());
      std::string s, stra, strb;
      while (std::getline(*input, s)) {
        try {
          std::string eol("\n");
          if (!cdelim.empty()) {
            std::string::size_type m = s.find(cdelim);
            if (m != std::string::npos) {
              eol = " " + s.substr(m) + "\n";
              s = s.substr(0, m);
            }
          }
          std::istringstream str(s);
          if (!(timeset || circle)) {
            if (!(str >> stra))
              throw GeographicErr("Incomplete input: " + s);
            time = Utility::fractionalyear<real>(stra);
            if (time < m.MinTime() - tguard || time > m.MaxTime() + tguard)
              throw GeographicErr("Time " + Utility::str(time) +
                                  " too far outside allowed range [" +
                                  Utility::str(m.MinTime()) + "," +
                                  Utility::str(m.MaxTime()) +
                                  "]");
            if (time < m.MinTime() || time > m.MaxTime())
              std::cerr << "WARNING: Time " << time
                        << " outside allowed range ["
                        << m.MinTime() << "," << m.MaxTime() << "]\n";
          }
          real lon;
          if (circle) {
            if (!(str >> strb))
              throw GeographicErr("Incomplete input: " + s);
            DMS::flag ind;
            lon = DMS::Decode(strb, ind);
            if (ind == DMS::LATITUDE)
              throw GeographicErr("Bad hemisphere letter on " + strb);
            if (lon < -180 || lon > 360)
              throw GeographicErr("Longitude " + strb + "not in [-180d, 360d]");
          } else {
            if (!(str >> stra >> strb))
              throw GeographicErr("Incomplete input: " + s);
            DMS::DecodeLatLon(stra, strb, lat, lon);
            h = 0;              // h is optional
            if (str >> h) {
              if (h < m.MinHeight() - hguard || h > m.MaxHeight() + hguard)
                throw GeographicErr("Height " + Utility::str(h/1000) +
                                    "km too far outside allowed range [" +
                                    Utility::str(m.MinHeight()/1000) + "km," +
                                    Utility::str(m.MaxHeight()/1000) + "km]");
              if (h < m.MinHeight() || h > m.MaxHeight())
                std::cerr << "WARNING: Height " << h/1000
                          << "km outside allowed range ["
                          << m.MinHeight()/1000 << "km,"
                          << m.MaxHeight()/1000 << "km]\n";
            }
            else
              str.clear();
          }
          if (str >> stra)
            throw GeographicErr("Extra junk in input: " + s);
          real bx, by, bz, bxt, byt, bzt;
          if (circle)
            c(lon, bx, by, bz, bxt, byt, bzt);
          else
            m(time, lat, lon, h, bx, by, bz, bxt, byt, bzt);
          real H, F, D, I, Ht, Ft, Dt, It;
          MagneticModel::FieldComponents(bx, by, bz, bxt, byt, bzt,
                                         H, F, D, I, Ht, Ft, Dt, It);

          *output << DMS::Encode(D, prec + 1, DMS::NUMBER) << " "
                  << DMS::Encode(I, prec + 1, DMS::NUMBER) << " "
                  << Utility::str<real>(H, prec) << " "
                  << Utility::str<real>(by, prec) << " "
                  << Utility::str<real>(bx, prec) << " "
                  << Utility::str<real>(-bz, prec) << " "
                  << Utility::str<real>(F, prec) << eol;
          if (rate)
            *output << DMS::Encode(Dt, prec + 1, DMS::NUMBER) << " "
                    << DMS::Encode(It, prec + 1, DMS::NUMBER) << " "
                    << Utility::str<real>(Ht, prec) << " "
                    << Utility::str<real>(byt, prec) << " "
                    << Utility::str<real>(bxt, prec) << " "
                    << Utility::str<real>(-bzt, prec) << " "
                    << Utility::str<real>(Ft, prec) << eol;
        }
        catch (const std::exception& e) {
          *output << "ERROR: " << e.what() << "\n";
          retval = 1;
        }
      }
    }
    catch (const std::exception& e) {
      std::cerr << "Error reading " << model << ": " << e.what() << "\n";
      retval = 1;
    }
    return retval;
  }
  catch (const std::exception& e) {
    std::cerr << "Caught exception: " << e.what() << "\n";
    return 1;
  }
  catch (...) {
    std::cerr << "Caught unknown exception\n";
    return 1;
  }
}
