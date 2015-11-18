#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <omp.h>
#include <mkl.h>
#define M_PI 3.14159265358979323846


typedef struct point
{
    double x;
    double y;
} point;


__attribute__( (target(mic)) ) point generate_sample(const int length, const int seed) {
    double angles[length];
    double steps[length];
    double res_x = 0;
    double res_y = 0;
    VSLStreamStatePtr rand_stream;
    vslNewStream(&rand_stream, VSL_BRNG_MT2203, seed*time(0));
    vdRngUniform(VSL_RNG_METHOD_UNIFORM_STD, rand_stream, length, angles, 0.0, 2*M_PI);
    vdRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF, rand_stream, length, steps, 10.0, 3);
    vslDeleteStream(&rand_stream);
    #pragma omp parallel for reduction(+:res_x, res_y)
    for (int i = 0; i < length; ++i) {
        res_x += steps[i]*cos(angles[i]);
        res_y += steps[i]*sin(angles[i]);
    }
    point result;
    result.x = res_x;
    result.y = res_y;
    return result;
}


point *built_full_path(const int length, const int seed) {
    double angles[length-1];
    double steps[length-1];
    point *path = (point *)memalign(64, length * sizeof(point));
    point start_point;
    start_point.x = 0;
    start_point.y = 0;
    path[0] = start_point;
    VSLStreamStatePtr rand_stream;
    vslNewStream(&rand_stream, VSL_BRNG_MT2203, seed*time(0));
    vdRngUniform(VSL_RNG_METHOD_UNIFORM_STD, rand_stream, length-1, angles, 0.0, 2*M_PI);
    vdRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF, rand_stream, length-1, steps, 10.0, 3);
    vslDeleteStream(&rand_stream);
    for (int i = 1; i < length; ++i) {
        point new_point;
        new_point.x = path[i-1].x + steps[i-1]*cos(angles[i-1]);
        new_point.y = path[i-1].y + steps[i-1]*sin(angles[i-1]);
        path[i] = new_point;
    }
    return path;
}


point *generate_whole_data(const int num_of_samples, const int steps) {
    point *results = (point *)memalign(64, num_of_samples * sizeof(point));
    #pragma offload target(mic) \
     out( results : length(num_of_samples) )
    {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < num_of_samples; ++i)
            results[i] = generate_sample(steps, i);
    }
    return results;
}


void write_points_data(point *points, const int length) {
    FILE *fp_points = fopen("points.dat", "w");
    fprintf(fp_points, "X Y\n");
    for (int i = 0; i < length; ++i)
        fprintf(fp_points, "%lf %lf\n", points[i].x, points[i].y);
    fclose(fp_points);
}


point **generate_paths_data(const int num_of_full_paths, const int length_of_path) {
    point **paths = (point **)memalign(64, num_of_full_paths * sizeof(point *));
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < num_of_full_paths; ++i)
        paths[i] = built_full_path(length_of_path, i);
    return paths;
}


void write_paths_data(point **paths, const int num_of_paths, const int length_of_path) {
    FILE *fp_paths = fopen("paths.dat", "w");
    for (int i = 0; i < num_of_paths; ++i) {
        for (int j = 0; j < length_of_path; ++j)
            fprintf(fp_paths, "%lf %lf\n", paths[i][j].x, paths[i][j].y);
        fprintf(fp_paths, "========\n");
    }
    fclose(fp_paths);
}


int main(int argc, char **argv) {
    const int steps = 100;
    const int num_of_samples = 1000000;
    const int num_of_full_paths = 4;
    point *set_of_points = generate_whole_data(num_of_samples, steps);
    write_points_data(set_of_points, num_of_samples);
    point **set_of_paths = generate_paths_data(num_of_full_paths, steps+1);
    write_paths_data(set_of_paths, num_of_full_paths, steps+1);
    free(set_of_points);
    for (int i = 0; i < num_of_full_paths; ++i)
        free(set_of_paths[i]);
    free(set_of_paths);
    return 0;
}
