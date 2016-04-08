// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Routeguide
{
    /// <summary>
    /// Utility methods for the route guide example.
    /// </summary>
    public static class RouteGuideUtil
    {
        public const string DefaultFeaturesFile = "route_guide_db.json";

        private const double CoordFactor = 1e7;

        /// <summary>
        /// Indicates whether the given feature exists (i.e. has a valid name).
        /// </summary>
        public static bool Exists(this Feature feature)
        {
            return feature != null && (feature.Name.Length != 0);
        }

        public static double GetLatitude(this Point point)
        {
            return point.Latitude / CoordFactor;
        }

        public static double GetLongitude(this Point point)
        {
            return point.Longitude / CoordFactor;
        }

        /// <summary>
        /// Calculate the distance between two points using the "haversine" formula.
        /// This code was taken from http://www.movable-type.co.uk/scripts/latlong.html.
        /// </summary>
        /// <param name="start">the starting point</param>
        /// <param name="end">the end point</param>
        /// <returns>the distance between the points in meters</returns>
        public static double GetDistance(this Point start, Point end)
        {
            double lat1 = start.GetLatitude();
            double lat2 = end.GetLatitude();
            double lon1 = start.GetLongitude();
            double lon2 = end.GetLongitude();
            int r = 6371000; // metres
            double phi1 = ToRadians(lat1);
            double phi2 = ToRadians(lat2);
            double deltaPhi = ToRadians(lat2 - lat1);
            double deltaLambda = ToRadians(lon2 - lon1);

            double a = Math.Sin(deltaPhi / 2) * Math.Sin(deltaPhi / 2) + Math.Cos(phi1) * Math.Cos(phi2) * Math.Sin(deltaLambda / 2) * Math.Sin(deltaLambda / 2);
            double c = 2 * Math.Atan2(Math.Sqrt(a), Math.Sqrt(1 - a));

            return r * c;
        }

        /// <summary>
        /// Returns <c>true</c> if rectangular area contains given point.
        /// </summary>
        public static bool Contains(this Rectangle rectangle, Point point)
        {
            int left = Math.Min(rectangle.Lo.Longitude, rectangle.Hi.Longitude);
            int right = Math.Max(rectangle.Lo.Longitude, rectangle.Hi.Longitude);
            int top = Math.Max(rectangle.Lo.Latitude, rectangle.Hi.Latitude);
            int bottom = Math.Min(rectangle.Lo.Latitude, rectangle.Hi.Latitude);
            return (point.Longitude >= left && point.Longitude <= right && point.Latitude >= bottom && point.Latitude <= top);
        }

        private static double ToRadians(double val)
        {
            return (Math.PI / 180) * val;
        }

        /// <summary>
        /// Parses features from a JSON file.
        /// </summary>
        public static List<Feature> ParseFeatures(string filename)
        {
            var features = new List<Feature>();
            var jsonFeatures = JsonConvert.DeserializeObject<List<JsonFeature>>(File.ReadAllText(filename));

            foreach(var jsonFeature in jsonFeatures)
            {
                features.Add(new Feature
                {
                    Name = jsonFeature.name,
                    Location = new Point { Longitude = jsonFeature.location.longitude, Latitude = jsonFeature.location.latitude}
                });
            }
            return features;
        }

        private class JsonFeature
        {
            public string name;
            public JsonLocation location;
        }

        private class JsonLocation
        {
            public int longitude;
            public int latitude;
        }
    }
}
