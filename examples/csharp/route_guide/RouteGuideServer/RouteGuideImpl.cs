using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Grpc.Core.Utils;

namespace Routeguide
{
    /// <summary>
    /// Example implementation of RouteGuide server.
    /// </summary>
    public class RouteGuideImpl : RouteGuide.IRouteGuide
    {
        readonly List<Feature> features;
        readonly object myLock = new object();
        readonly Dictionary<Point, List<RouteNote>> routeNotes = new Dictionary<Point, List<RouteNote>>();   

        public RouteGuideImpl(List<Feature> features)
        {
            this.features = features;
        }

        /// <summary>
        /// Gets the feature at the requested point. If no feature at that location
        /// exists, an unnammed feature is returned at the provided location.
        /// </summary>
        public Task<Feature> GetFeature(Point request, Grpc.Core.ServerCallContext context)
        {
            return Task.FromResult(CheckFeature(request));
        }

        /// <summary>
        /// Gets all features contained within the given bounding rectangle.
        /// </summary>
        public async Task ListFeatures(Rectangle request, Grpc.Core.IServerStreamWriter<Feature> responseStream, Grpc.Core.ServerCallContext context)
        {
            var responses = features.FindAll( (feature) => feature.Exists() && request.Contains(feature.Location) );
            foreach (var response in responses)
            {
                await responseStream.WriteAsync(response);
            }
        }

        /// <summary>
        /// Gets a stream of points, and responds with statistics about the "trip": number of points,
        /// number of known features visited, total distance traveled, and total time spent.
        /// </summary>
        public async Task<RouteSummary> RecordRoute(Grpc.Core.IAsyncStreamReader<Point> requestStream, Grpc.Core.ServerCallContext context)
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
                if (CheckFeature(point).Exists())
                {
                    featureCount++;
                }
                if (previous != null)
                {
                    distance += (int) previous.GetDistance(point);
                }
                previous = point;
            }

            stopwatch.Stop();
            
            return new RouteSummary
            {
                PointCount = pointCount,
                FeatureCount = featureCount,
                Distance = distance,
                ElapsedTime = (int)(stopwatch.ElapsedMilliseconds / 1000)
            };
        }

        /// <summary>
        /// Receives a stream of message/location pairs, and responds with a stream of all previous
        /// messages at each of those locations.
        /// </summary>
        public async Task RouteChat(Grpc.Core.IAsyncStreamReader<RouteNote> requestStream, Grpc.Core.IServerStreamWriter<RouteNote> responseStream, Grpc.Core.ServerCallContext context)
        {
            while (await requestStream.MoveNext())
            {
                var note = requestStream.Current;
                List<RouteNote> prevNotes = AddNoteForLocation(note.Location, note);
                foreach (var prevNote in prevNotes)
                {
                    await responseStream.WriteAsync(prevNote);
                }
            }
        }

        /// <summary>
        /// Adds a note for location and returns a list of pre-existing notes for that location (not containing the newly added note).
        /// </summary>
        private List<RouteNote> AddNoteForLocation(Point location, RouteNote note)
        {
            lock (myLock)
            {
                List<RouteNote> notes;
                if (!routeNotes.TryGetValue(location, out notes)) {
                    notes = new List<RouteNote>();
                    routeNotes.Add(location, notes);
                }
                var preexistingNotes = new List<RouteNote>(notes);
                notes.Add(note);
                return preexistingNotes;
            }
        }

        /// <summary>
        /// Gets the feature at the given point.
        /// </summary>
        /// <param name="location">the location to check</param>
        /// <returns>The feature object at the point Note that an empty name indicates no feature.</returns>
        private Feature CheckFeature(Point location)
        {
            var result = features.FirstOrDefault((feature) => feature.Location.Equals(location));
            if (result == null)
            {
                // No feature was found, return an unnamed feature.
                return new Feature { Name = "", Location = location };
            }
            return result;
        }
    }
}
