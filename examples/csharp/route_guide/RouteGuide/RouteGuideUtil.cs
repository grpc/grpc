using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace examples
{
    public static class RouteGuideUtil
    {
        public const string DefaultFeaturesFile = "route_guide_db.json";

        private const double CoordFactor = 1e7;

        /// <summary>
        /// Indicates whether the given feature exists (i.e. has a valid name).
        /// </summary>
        public static bool Exists(Feature feature)
        {
            return feature != null && (feature.Name.Length != 0);
        }

        public static double GetLatitude(Point point)
        {
            return point.Latitude / CoordFactor;
        }

        public static double GetLongitude(Point point)
        {
            return point.Longitude / CoordFactor;
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
                features.Add(Feature.CreateBuilder().SetName(jsonFeature.name).SetLocation(
                    Point.CreateBuilder()
                        .SetLongitude(jsonFeature.location.longitude)
                        .SetLatitude(jsonFeature.location.latitude).Build()).Build());
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
