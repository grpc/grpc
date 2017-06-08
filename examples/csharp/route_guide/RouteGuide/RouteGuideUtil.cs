// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
