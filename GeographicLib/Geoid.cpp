/**
 * \file Geoid.cpp
 * \brief Implementation for GeographicLib::Geoid class
 *
 * Copyright (c) Charles Karney (2009) <charles@karney.com>
 * and licensed under the LGPL.  For more information, see
 * http://charles.karney.info/geographic/
 **********************************************************************/

#include "GeographicLib/Geoid.hpp"
#include <sstream>
#include <limits>
#include <cstdlib>
#include <stdexcept>
#include <cmath>
#include <algorithm>

#define GEOGRAPHICLIB_GEOID_CPP "$Id$"

RCSID_DECL(GEOGRAPHICLIB_GEOID_CPP)
RCSID_DECL(GEOGRAPHICLIB_GEOID_HPP)

#if !defined(GEOID_DEFAULT_PATH)
#if defined(_MSC_VER)
#define GEOID_DEFAULT_PATH "C:/cygwin/usr/local/share/GeographicLib/geoids"
#else
#define GEOID_DEFAULT_PATH "/usr/local/share/GeographicLib/geoids"
#endif
#endif

#if defined(_MSC_VER)
// Squelch warnings about unsafe use of getenv
#pragma warning (disable: 4996)
#endif

namespace GeographicLib {

  using namespace std;

  // This is the transfer matrix for a 3rd order fit with a 12-point stencil
  // with weights
  //
  //  \ x -1  0  1  2
  //  y
  //  -1   .  1  1  .
  //   0   1  2  2  1
  //   1   1  2  2  1
  //   2   .  1  1  .
  //
  // A algorithm for n-dimensional polynomial fits is described in
  //   F. H. Lesh,
  //   Multi-dimensional least-squares polynomial curve fitting,
  //   CACM 2, 29-30 (1959).
  //
  // Here's the Maxima code to generate this matrix:
  //
  // /* The stencil and the weights */
  // xarr:[
  //     0, 1,
  // -1, 0, 1, 2,
  // -1, 0, 1, 2,
  //     0, 1]$
  // yarr:[
  //   -1,-1,
  // 0, 0, 0, 0,
  // 1, 1, 1, 1,
  //    2, 2]$
  // warr:[
  //    1, 1,
  // 1, 2, 2, 1,
  // 1, 2, 2, 1,
  //    1, 1]$
  // 
  // /* [x exponent, y exponent] for cubic fit */
  // pows:[
  // [0,0],
  // [1,0],[0,1],
  // [2,0],[1,1],[0,2],
  // [3,0],[2,1],[1,2],[0,3]]$
  // 
  // basisvec(x,y,pows):=map(lambda([ex],(if ex[1]=0 then 1 else x^ex[1])*
  //     (if ex[2]=0 then 1 else y^ex[2])),pows)$
  // addterm(x,y,f,w,pows):=block([a,b,bb:basisvec(x,y,pows)],
  //   a:w*(transpose(bb).bb),
  //   b:(w*f) * bb,
  //   [a,b])$
  // 
  // c3row(k):=block([a,b,c,pows:pows,n],
  //   n:length(pows),
  //   a:zeromatrix(n,n),
  //   b:copylist(part(a,1)),
  //   c:[a,b],
  //   for i:1 thru length(xarr) do
  //   c:c+addterm(xarr[i],yarr[i],if i=k then 1 else 0,warr[i],pows),
  //   a:c[1],b:c[2],
  //   part(transpose( a^^-1 . transpose(b)),1))$
  // c3:[]$
  // for k:1 thru length(warr) do c3:endcons(c3row(k),c3)$
  // c3:apply(matrix,c3)$
  // c0:part(ratsimp(
  // genmatrix(yc,1,length(warr)).abs(c3).genmatrix(yd,length(pows),1)),2)$
  // c3:c0*c3$

  const Math::real_t Geoid::c0 = 240; // Common denominator
  const Math::real_t Geoid::c3[stencilsize * nterms] = {
      9, -18, -88,    0,  96,   90,   0,   0, -60, -20,
     -9,  18,   8,    0, -96,   30,   0,   0,  60, -20,
      9, -88, -18,   90,  96,    0, -20, -60,   0,   0,
    186, -42, -42, -150, -96, -150,  60,  60,  60,  60,
     54, 162, -78,   30, -24,  -90, -60,  60, -60,  60,
     -9, -32,  18,   30,  24,    0,  20, -60,   0,   0,
     -9,   8,  18,   30, -96,    0, -20,  60,   0,   0,
     54, -78, 162,  -90, -24,   30,  60, -60,  60, -60,
    -54,  78,  78,   90, 144,   90, -60, -60, -60, -60,
      9,  -8, -18,  -30, -24,    0,  20,  60,   0,   0,
     -9,  18, -32,    0,  24,   30,   0,   0, -60,  20,
      9, -18,  -8,    0, -24,  -30,   0,   0,  60,  20,
  };

  // Like c3, but with the coeffs of x, x^2, and x^3 constrained to be zero.
  // Use this at the N pole so that the height in independent of the longitude
  // there.
  //
  // Here's the Maxima code to generate this matrix (continued from above).
  // 
  // /* figure which terms to exclude so that fit is indep of x at y=0 */
  // mask:part(zeromatrix(1,length(pows)),1)+1$
  // for i:1 thru length(pows) do
  // if pows[i][1]>0 and pows[i][2]=0 then mask[i]:0$
  // 
  // /* Same as c3row but with masked pows. */
  // c3nrow(k):=block([a,b,c,powsa:[],n,d,e],
  //   for i:1 thru length(mask) do if mask[i]>0 then
  //   powsa:endcons(pows[i],powsa),
  //   n:length(powsa),
  //   a:zeromatrix(n,n),
  //   b:copylist(part(a,1)),
  //   c:[a,b],
  //   for i:1 thru length(xarr) do
  //   c:c+addterm(xarr[i],yarr[i],if i=k then 1 else 0,warr[i],powsa),
  //   a:c[1],b:c[2],
  //   d:part(transpose( a^^-1 . transpose(b)),1),
  //   e:[],
  //   for i:1 thru length(mask) do
  //   if mask[i]>0 then (e:endcons(first(d),e),d:rest(d)) else e:endcons(0,e),
  //   e)$
  // c3n:[]$
  // for k:1 thru length(warr) do c3n:endcons(c3nrow(k),c3n)$
  // c3n:apply(matrix,c3n)$
  // c0n:part(ratsimp(
  //     genmatrix(yc,1,length(warr)).abs(c3n).genmatrix(yd,length(pows),1)),2)$
  // c3n:c0n*c3n$

  const Math::real_t Geoid::c0n = 372; // Common denominator
  const Math::real_t Geoid::c3n[stencilsize * nterms] = {
      0, 0, -131, 0,  138,  144, 0,   0, -102, -31,
      0, 0,    7, 0, -138,   42, 0,   0,  102, -31,
     62, 0,  -31, 0,    0,  -62, 0,   0,    0,  31,
    124, 0,  -62, 0,    0, -124, 0,   0,    0,  62,
    124, 0,  -62, 0,    0, -124, 0,   0,    0,  62,
     62, 0,  -31, 0,    0,  -62, 0,   0,    0,  31,
      0, 0,   45, 0, -183,   -9, 0,  93,   18,   0,
      0, 0,  216, 0,   33,   87, 0, -93,   12, -93,
      0, 0,  156, 0,  153,   99, 0, -93,  -12, -93,
      0, 0,  -45, 0,   -3,    9, 0,  93,  -18,   0,
      0, 0,  -55, 0,   48,   42, 0,   0,  -84,  31,
      0, 0,   -7, 0,  -48,  -42, 0,   0,   84,  31,
  };

  // Like c3n, but y -> 1-y so that h is independent of x at y = 1.  Use this
  // at the S pole so that the height in independent of the longitude there.
  //
  // Here's the Maxima code to generate this matrix (continued from above).
  // 
  // /* Transform c3n to c3s by transforming y -> 1-y */
  // vv:[
  //      v[11],v[12],
  // v[7],v[8],v[9],v[10],
  // v[3],v[4],v[5],v[6],
  //      v[1],v[2]]$
  // poly:expand(vv.(c3n/c0n).transpose(basisvec(x,1-y,pows)))$
  // c3sf[i,j]:=coeff(coeff(coeff(poly,v[i]),x,pows[j][1]),y,pows[j][2])$
  // c3s:genmatrix(c3sf,length(vv),length(pows))$
  // c0s:part(ratsimp(
  //     genmatrix(yc,1,length(warr)).abs(c3s).genmatrix(yd,length(pows),1)),2)$
  // c3s:c0s*c3s$

  const Math::real_t Geoid::c0s = 372; // Common denominator
  const Math::real_t Geoid::c3s[stencilsize * nterms] = {
     18,  -36, -122,   0,  120,  135, 0,   0,  -84, -31,
    -18,   36,   -2,   0, -120,   51, 0,   0,   84, -31,
     36, -165,  -27,  93,  147,   -9, 0, -93,   18,   0,
    210,   45, -111, -93,  -57, -192, 0,  93,   12,  93,
    162,  141,  -75, -93, -129, -180, 0,  93,  -12,  93,
    -36,  -21,   27,  93,   39,    9, 0, -93,  -18,   0,
      0,    0,   62,   0,    0,   31, 0,   0,    0, -31,
      0,    0,  124,   0,    0,   62, 0,   0,    0, -62,
      0,    0,  124,   0,    0,   62, 0,   0,    0, -62,
      0,    0,   62,   0,    0,   31, 0,   0,    0, -31,
    -18,   36,  -64,   0,   66,   51, 0,   0, -102,  31,
     18,  -36,    2,   0,  -66,  -51, 0,   0,  102,  31,
  };

  Geoid::Geoid(const std::string& name, const std::string& path, bool cubic)
    : _cubic(cubic)
    , _a( Constants::WGS84_a() )
    , _e2( (2 - 1/Constants::WGS84_r())/Constants::WGS84_r() )
    , _degree( Constants::degree() )
    , _eps( sqrt(numeric_limits<real_t>::epsilon()) ) {
    string dir = path;
    if (dir.size() == 0)
      dir = GeoidPath();
    if (dir.size() == 0)
      dir = DefaultPath();
    _filename = dir + "/" + name + ".pgm";
    _file.open(_filename.c_str(), ios::binary);
    if (!(_file.good()))
      throw out_of_range("File not readable " + _filename);
    string s;
    if (!(getline(_file, s) && s == "P5"))
      throw out_of_range("File not in PGM format " + _filename);
    _offset = numeric_limits<real_t>::max();
    _scale = 0;
    _maxerror = _rmserror = -1;
    _description = "NONE";
    _datetime = "UNKNOWN";
    while (getline(_file, s)) {
      if (s.size() == 0)
	continue;
      if (s[0] == '#') {
	if (s.substr(0, 14) == "# Description ")
	  _description = s.substr(14);
	else if (s.substr(0, 11) == "# DateTime ")
	  _datetime = s.substr(11);
	else if (s.substr(0,9) == "# Offset ") {
	  s = s.substr(9);
	  istringstream is(s);
	  if (!(is >> _offset))
	    throw out_of_range("Error reading offset " + _filename);
	} else if (s.substr(0, 8) == "# Scale ") {
	  s = s.substr(8);
	  istringstream is(s);
	  if (!(is >> _scale))
	    throw out_of_range("Error reading scale " + _filename);
	} else if (!_cubic && s.substr(0,19) == "# MaxBilinearError ") {
	  s = s.substr(19);
	  istringstream is(s);
	  // It's not an error if the error can't be read
	  is >> _maxerror;
	} else if (!_cubic && s.substr(0,19) == "# RMSBilinearError ") {
	  s = s.substr(19);
	  istringstream is(s);
	  // It's not an error if the error can't be read
	  is >> _rmserror;
	} else if (_cubic && s.substr(0,16) == "# MaxCubicError ") {
	  s = s.substr(16);
	  istringstream is(s);
	  // It's not an error if the error can't be read
	  is >> _maxerror;
	} else if (_cubic && s.substr(0,16) == "# RMSCubicError ") {
	  s = s.substr(16);
	  istringstream is(s);
	  // It's not an error if the error can't be read
	  is >> _rmserror;
	}
      } else {
	istringstream is(s);
	if (!(is >> _width >> _height))
	  throw out_of_range("Error reading raster size " + _filename);
	break;
      }
    }
    {
      unsigned maxval;
      if (!(_file >> maxval))
	throw out_of_range("Error reading maxval " + _filename);
      if (maxval != 0xffffu)
	throw out_of_range("Maxval not equal to 2^16-1 " + _filename);
      // Add 1 for whitespace after maxval
      _datastart = unsigned(_file.tellg()) + 1u;
    }
    if (_offset == numeric_limits<real_t>::max())
      throw out_of_range("Offset not set " + _filename);
    if (_scale == 0)
      throw out_of_range("Scale not set " + _filename);
    if (_scale < 0)
      throw out_of_range("Scale must be positive " + _filename);
    if (_height < 2 || _width < 2)
      // Coarsest grid spacing is 180deg.
      throw out_of_range("Raster size too small " + _filename);
    if (_width & 1)
      // This is so that longitude grids can be extended thru the poles.
      throw out_of_range("Raster width is odd " + _filename);
    _file.seekg(0, ios::end);
    if (_datastart + 2 * _width * _height != _file.tellg())
      // Possibly this test should be "<" because the tile contains, e.g., a
      // second image.  However, for now we are more strict.
      throw out_of_range("File has the wrong length " + _filename);
    _rlonres = _width / real_t(360);
    _rlatres = (_height - 1) / real_t(180);
    _cache = false;
    _ix = _width;
    _iy = _height;
    // Ensure that file errors throw exceptions
    _file.exceptions(ifstream::eofbit | ifstream::failbit | ifstream::badbit);
  }

  Math::real_t Geoid::height(real_t lat, real_t lon, bool gradp,
			     real_t& gradn, real_t& grade) const  {
    if (lon < 0)
      lon += 360;
    real_t
      fy = (90 - lat) * _rlatres,
      fx = lon * _rlonres;
    int
      iy = int(fy),
      ix = int(fx);
    if (iy == _height - 1)
      --iy;
    fx -= ix;
    fy -= iy;
    if (!(ix == _ix && iy == _iy)) {
      _ix = ix;
      _iy = iy;
      if (!_cubic) {
	_v00 = rawval(ix    , iy    );
	_v01 = rawval(ix + 1, iy    );
	_v10 = rawval(ix    , iy + 1);
	_v11 = rawval(ix + 1, iy + 1);
      } else {
	real_t v[stencilsize];
	int k = 0;
	v[k++] = rawval(ix    , iy - 1);
	v[k++] = rawval(ix + 1, iy - 1);
	v[k++] = rawval(ix - 1, iy    );
	v[k++] = rawval(ix    , iy    );
	v[k++] = rawval(ix + 1, iy    );
	v[k++] = rawval(ix + 2, iy    );
	v[k++] = rawval(ix - 1, iy + 1);
	v[k++] = rawval(ix    , iy + 1);
	v[k++] = rawval(ix + 1, iy + 1);
	v[k++] = rawval(ix + 2, iy + 1);
	v[k++] = rawval(ix    , iy + 2);
	v[k++] = rawval(ix + 1, iy + 2);

	const real_t* c3x = iy == 0 ? c3n : iy == _height - 2 ? c3s : c3;
	real_t c0x = iy == 0 ? c0n : iy == _height - 2 ? c0s : c0;
	for (unsigned i = 0; i < nterms; ++i) {
	  _t[i] = 0;
	  for (unsigned j = 0; j < stencilsize; ++j)
	    _t[i] += v[j] * c3x[nterms * j + i];
	  _t[i] /= c0x;
	}
      }
    }
    if (!_cubic) {
      real_t
	a = (1 - fx) * _v00 + fx * _v01,
	b = (1 - fx) * _v10 + fx * _v11,
	c = (1 - fy) * a + fy * b,
	h = _offset + _scale * c;
      if (gradp) {
	real_t
	  phi = lat * _degree,
	  cosphi = cos(phi),
	  sinphi = sin(phi),
	  n = 1/sqrt(1 - _e2 * sinphi * sinphi);
	gradn = ((1 - fx) * (_v00 - _v10) + fx * (_v01 - _v11)) *
	  _rlatres / (_degree * _a * (1 - _e2) * n * n * n);
	grade = (cosphi > _eps ?
		 ((1 - fy) * (_v01 - _v00) + fy * (_v11 - _v10)) /   cosphi :
		 (sinphi > 0 ? _v11 - _v10 : _v01 - _v00) *
		 _rlatres / _degree ) *
	  _rlonres / (_degree * _a * n);
	gradn *= _scale;
	grade *= _scale;
      }
      return h;
    } else {
      real_t h = _t[0] + fx * (_t[1] + fx * (_t[3] + fx * _t[6])) +
	fy * (_t[2] + fx * (_t[4] + fx * _t[7]) +
	     fy * (_t[5] + fx * _t[8] + fy * _t[9]));
      h = _offset + _scale * h;
      if (gradp) {
	// Avoid 0/0 at the poles by backing off 1/100 of a cell size
	lat = min(lat,  90 - 1/(100 * _rlatres));
	lat = max(lat, -90 + 1/(100 * _rlatres));
	fy = (90 - lat) * _rlatres;
	fy -=  int(fy);
	real_t
	  phi = lat * _degree,
	  cosphi = cos(phi),
	  sinphi = sin(phi),
	  n = 1/sqrt(1 - _e2 * sinphi * sinphi);
	gradn = _t[2] + fx * (_t[4] + fx * _t[7]) +
	  fy * (2 * _t[5] + fx * 2 * _t[8] + 3 * fy * _t[9]);
	grade = _t[1] + fx * (2 * _t[3] + fx * 3 * _t[6]) +
	  fy * (_t[4] + fx * 2 * _t[7] + fy * _t[8]);
	gradn *= - _rlatres / (_degree * _a * (1 - _e2) * n * n * n) * _scale;
	grade *= _rlonres / (_degree * _a * n * cosphi) * _scale;
      }
      return h;
    }
  }

  void Geoid::CacheClear() const throw() {
    _cache = false;
    try {
      _data.clear();
      // Use swap to release memory back to system
      vector< vector<unsigned short> > t;
      _data.swap(t);
    }
    catch (exception&) {
    }
  }

  void Geoid::CacheArea(real_t south, real_t west,
			real_t north, real_t east) const {
    if (south > north) {
      CacheClear();
      return;
    }
    west += west < 0 ? 360 : west >= 360 ? -360 : 0;
    east += east < 0 ? 360 : east >= 360 ? -360 : 0;
    if (east <= west)
      east += 360;
    // Move south (and north) boundaries off the south pole.
    south = min(south, -90 + real_t(0.5L) / _rlatres);
    north = min(north, south);
    real_t
      fn = (90 - north) * _rlatres,
      fs = (90 - south) * _rlatres,
      fw = west * _rlonres,
      fe = east * _rlonres;
    // Extra boudary of cells for cubic interpolation
    int boundary = _cubic ? 1 : 0;
    // Bounding indices for cached area
    int
      in = int(fn)     - boundary,
      is = int(fs) + 1 + boundary,
      iw = int(fw)     - boundary,
      ie = int(fe) + 1 + boundary;
    if (is >= _height)
      is = _height - 1;
    if (in < 0)
      in = 0;
    if (ie - iw >= _width - 1) {
      // Include entire longitude range
      iw = 0;
      ie = _width - 1;
    }
    if (iw < 0){
      iw += _width;
      ie += _width;
    }
    int oysize = int(_data.size());
    _xsize = ie - iw + 1;
    _ysize = is - in + 1;
    _xoffset = iw;
    _yoffset = in;

    try {
      _data.resize(_ysize, vector<unsigned short>(_xsize));
      for (int iy = min(oysize, _ysize); iy--;)
	_data[iy].resize(_xsize);
    }
    catch (bad_alloc&) {
      CacheClear();
      throw out_of_range("Insufficient memory for caching " + _filename);
    }

    try {
      int
	ie1 = min(_width - 1, ie),
	w1 = ie1 - iw + 1;
      vector<char> buf(2 * w1);
      for (int iy = in; iy <= is; ++iy) {
	_file.seekg(_datastart + 2 * (iy * _width + iw), ios::beg);
	_file.read(&(buf[0]), 2 * w1);
	for (int ix = 0; ix < w1; ++ix)
	  _data[iy - in][ix] =
	    (unsigned short)((unsigned char)buf[2 * ix] * 256u +
			     (unsigned char)buf[2 * ix + 1]);
      }
      if (ie1 < ie) {
	// Cached area wraps past longitude = 0
	ie1 = ie - _width;
	int
	  iw1 = 0,
	  w2 = ie1 + 1;
	buf.resize(2 * w1);
	for (int iy = in; iy <= is; ++iy) {
	  _file.seekg(_datastart + 2 * (iy * _width + iw1), ios::beg);
	  _file.read(&(buf[0]), 2 * w2);
	  for (int ix = 0; ix < w2; ++ix)
	  _data[iy - in][ix + w1] =
	    (unsigned short)((unsigned char)buf[2 * ix] * 256u +
			     (unsigned char)buf[2 * ix + 1]);
	}
      }
      _cache = true;
    }
    catch (exception& e) {
      CacheClear();
      throw out_of_range(string("Error filling cache ") + e.what());
    }
  }

  std::string Geoid::DefaultPath() {
    return string(GEOID_DEFAULT_PATH);
  }

  std::string Geoid::GeoidPath() {
    string path;
    char* geoidpath = getenv("GEOID_PATH");
    if (geoidpath)
      path = string(geoidpath);
    return path;
  }
}