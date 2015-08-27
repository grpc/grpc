using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace examples
{
    /// <summary>
    /// Example implementation of RouteGuide server.
    /// </summary>
    public class RouteGuideImpl : RouteGuide.IRouteGuide
    {
        readonly List<Feature> features;
        private readonly ConcurrentDictionary<Point, List<RouteNote>> routeNotes =
            new ConcurrentDictionary<Point, List<RouteNote>>();

        public RouteGuideImpl(List<Feature> features)
        {
            this.features = features;
        }

        /// <summary>
        /// Gets the feature at the requested point. If no feature at that location
        /// exists, an unnammed feature is returned at the provided location.
        /// </summary>
        public Task<Feature> GetFeature(Grpc.Core.ServerCallContext context, Point request)
        {
            return Task.FromResult(CheckFeature(request));
        }

        /// <summary>
        /// Gets all features contained within the given bounding rectangle.
        /// </summary>
        public async Task ListFeatures(Grpc.Core.ServerCallContext context, Rectangle request, Grpc.Core.IServerStreamWriter<Feature> responseStream)
        {
            int left = Math.Min(request.Lo.Longitude, request.Hi.Longitude);
            int right = Math.Max(request.Lo.Longitude, request.Hi.Longitude);
            int top = Math.Max(request.Lo.Latitude, request.Hi.Latitude);
            int bottom = Math.Min(request.Lo.Latitude, request.Hi.Latitude);

            foreach (var feature in features)
            {
                if (!RouteGuideUtil.Exists(feature))
                {
                    continue;
                }

                int lat = feature.Location.Latitude;
                int lon = feature.Location.Longitude;
                if (lon >= left && lon <= right && lat >= bottom && lat <= top)
                {
                    await responseStream.WriteAsync(feature);
                }
            }
        }

        /// <summary>
        /// Gets a stream of points, and responds with statistics about the "trip": number of points,
        /// number of known features visited, total distance traveled, and total time spent.
        /// </summary>
        public async Task<RouteSummary> RecordRoute(Grpc.Core.ServerCallContext context, Grpc.Core.IAsyncStreamReader<Point> requestStream)
        {
            int pointCount = 0;
            int featureCount = 0;
            int distance = 0;
            Point previous = null;
            var stopwatch = new Stopwatch();
            stopwatch.Start();

            while (await requestStream.MoveNext())
            {
                var point = requestStream.Current;
                pointCount++;
                if (RouteGuideUtil.Exists(CheckFeature(point)))
                {
                    featureCount++;
                }
                if (previous != null)
                {
                    distance += (int) CalcDistance(previous, point);
                }
                previous = point;
            }

            stopwatch.Stop();
            return RouteSummary.CreateBuilder().SetPointCount(pointCount)
                .SetFeatureCount(featureCount).SetDistance(distance)
                .SetElapsedTime((int) (stopwatch.ElapsedMilliseconds / 1000)).Build();
        }

        /// <summary>
        /// Receives a stream of message/location pairs, and responds with a stream of all previous
        /// messages at each of those locations.
        /// </summary>
        public async Task RouteChat(Grpc.Core.ServerCallContext context, Grpc.Core.IAsyncStreamReader<RouteNote> requestStream, Grpc.Core.IServerStreamWriter<RouteNote> responseStream)
        {
            while (await requestStream.MoveNext())
            {
                var note = requestStream.Current;
                List<RouteNote> notes = GetOrCreateNotes(note.Location);

                List<RouteNote> prevNotes;
                lock (notes)
                {
                    prevNotes = new List<RouteNote>(notes);
                }

                foreach (var prevNote in prevNotes)
                {
                    await responseStream.WriteAsync(prevNote);
                }                
                
                lock (notes)
                {
                    notes.Add(note);
                }
            }
        }

        
        /// <summary>
        /// Get the notes list for the given location. If missing, create it.
        /// </summary>
        private List<RouteNote> GetOrCreateNotes(Point location)
        {
            List<RouteNote> notes = new List<RouteNote>();
            routeNotes.TryAdd(location, notes);
            return routeNotes[location];
        }

        /// <summary>
        /// Gets the feature at the given point.
        /// </summary>
        /// <param name="location">the location to check</param>
        /// <returns>The feature object at the point Note that an empty name indicates no feature.</returns>
        private Feature CheckFeature(Point location)
        {
            foreach (var feature in features)
            {
                if (feature.Location.Latitude == location.Latitude
                    && feature.Location.Longitude == location.Longitude)
                {
                    return feature;
                }
            }

            // No feature was found, return an unnamed feature.
            return Feature.CreateBuilder().SetName("").SetLocation(location).Build();
        }

        /// <summary>
        /// Calculate the distance between two points using the "haversine" formula.
        /// This code was taken from http://www.movable-type.co.uk/scripts/latlong.html.
        /// </summary>
        /// <param name="start">the starting point</param>
        /// <param name="end">the end point</param>
        /// <returns>the distance between the points in meters</returns>
        private static double CalcDistance(Point start, Point end)
        {
            double lat1 = RouteGuideUtil.GetLatitude(start);
            double lat2 = RouteGuideUtil.GetLatitude(end);
            double lon1 = RouteGuideUtil.GetLongitude(start);
            double lon2 = RouteGuideUtil.GetLongitude(end);
            int r = 6371000; // metres
            double φ1 = ToRadians(lat1);
            double φ2 = ToRadians(lat2);
            double Δφ = ToRadians(lat2 - lat1);
            double Δλ = ToRadians(lon2 - lon1);

            double a = Math.Sin(Δφ / 2) * Math.Sin(Δφ / 2) + Math.Cos(φ1) * Math.Cos(φ2) * Math.Sin(Δλ / 2) * Math.Sin(Δλ / 2);
            double c = 2 * Math.Atan2(Math.Sqrt(a), Math.Sqrt(1 - a));

            return r * c;
        }

        private static double ToRadians(double val)
        {
            return (Math.PI / 180) * val;
        }
    }
}
