/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2024 by Javier Almonacid
 *
 * This file is part of the Flexodeal library
 *
 * The Flexodeal library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE.md at
 * the top level directory of Flexodeal.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Javier Almonacid, Simon Fraser University
 */


// We start by including all the necessary deal.II header files and some C++
// related ones. They have been discussed in detail in previous tutorial
// programs, so you need only refer to past tutorials for details.
#include <deal.II/base/function.h>
#include <deal.II/base/parameter_handler.h>
#include <deal.II/base/point.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/symmetric_tensor.h>
#include <deal.II/base/tensor.h>
#include <deal.II/base/timer.h>
#include <deal.II/base/work_stream.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>

// This header gives us the functionality to store
// data at quadrature points
#include <deal.II/base/quadrature_point_data.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/grid_in.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/tria.h>

#include <deal.II/fe/fe_dgp_monomial.h>
#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_tools.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/mapping_q_eulerian.h>

#include <deal.II/lac/block_sparse_matrix.h>
#include <deal.II/lac/block_vector.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/precondition_selector.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/solver_selector.h>
#include <deal.II/lac/sparse_direct.h>
#include <deal.II/lac/affine_constraints.h>

// Here are the headers necessary to use the LinearOperator class.
// These are also all conveniently packaged into a single
// header file, namely <deal.II/lac/linear_operator_tools.h>
// but we list those specifically required here for the sake
// of transparency.
#include <deal.II/lac/linear_operator.h>
#include <deal.II/lac/packaged_operation.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/vector_tools.h>

// Defined in these two headers are some operations that are pertinent to
// finite strain elasticity. The first will help us compute some kinematic
// quantities, and the second provides some stanard tensor definitions.
#include <deal.II/physics/elasticity/kinematics.h>
#include <deal.II/physics/elasticity/standard_tensors.h>

#include <iostream>
#include <fstream>


// We then stick everything that relates to this tutorial program into a
// namespace of its own, and import all the deal.II function and class names
// into it:
namespace Flexodeal
{
  using namespace dealii;

  // @sect3{Run-time parameters}
  //
  // There are several parameters that can be set in the code so we set up a
  // ParameterHandler object to read in the choices at run-time.
  namespace Parameters
  {
    // @sect4{Finite Element system}

    // As mentioned in the introduction, a different order interpolation should
    // be used for the displacement $\mathbf{u}$ than for the pressure
    // $\widetilde{p}$ and the dilatation $\widetilde{J}$.  Choosing
    // $\widetilde{p}$ and $\widetilde{J}$ as discontinuous (constant) functions
    // at the element level leads to the mean-dilatation method. The
    // discontinuous approximation allows $\widetilde{p}$ and $\widetilde{J}$ to
    // be condensed out and a classical displacement based method is recovered.
    // Here we specify the polynomial order used to approximate the solution.
    // The quadrature order should be adjusted accordingly.
    struct FESystem
    {
      unsigned int poly_degree;
      unsigned int quad_order;
      std::string  type_of_simulation;

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);
    };


    void FESystem::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Finite element system");
      {
        prm.declare_entry("Polynomial degree",
                          "2",
                          Patterns::Integer(0),
                          "Displacement system polynomial order");

        prm.declare_entry("Quadrature order",
                          "3",
                          Patterns::Integer(0),
                          "Gauss quadrature order");

        prm.declare_entry("Type of simulation",
                          "quasi-static",
                          Patterns::Selection("quasi-static|dynamic"),
                          "Type of simulation (quasi-static or dynamic)");
      }
      prm.leave_subsection();
    }

    void FESystem::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Finite element system");
      {
        poly_degree = prm.get_integer("Polynomial degree");
        quad_order  = prm.get_integer("Quadrature order");
        type_of_simulation = prm.get("Type of simulation");
      }
      prm.leave_subsection();
    }

    // @sect4{Geometry}

    // Make adjustments to the problem geometry and the applied load.  Since the
    // problem modelled here is quite specific, the load scale can be altered to
    // specific values to compare with the results given in the literature.
    struct Geometry
    {
      unsigned int global_refinement;
      double       length;
      double       width;
      double       height;
      double       scale;
      double       p_p0;

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);
    };

    void Geometry::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Geometry");
      {
        prm.declare_entry("Global refinement",
                          "2",
                          Patterns::Integer(0),
                          "Global refinement level");
        
        prm.declare_entry("Length", "1.0",
                          Patterns::Double(0.0),
                          "Length in the x direction");

        prm.declare_entry("Width", "1.0",
                          Patterns::Double(0.0),
                          "Width in the y direction");

        prm.declare_entry("Height", "1.0",
                          Patterns::Double(0.0),
                          "Height in the z direction");

        prm.declare_entry("Grid scale",
                          "1e-3",
                          Patterns::Double(0.0),
                          "Global grid scaling factor");

        prm.declare_entry("Pressure ratio p/p0",
                          "100",
                          Patterns::Selection("0|20|40|60|80|100"),
                          "Ratio of applied pressure to reference pressure");
      }
      prm.leave_subsection();
    }

    void Geometry::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Geometry");
      {
        global_refinement = prm.get_integer("Global refinement");
        length            = prm.get_double("Length"); 
        width             = prm.get_double("Width"); 
        height            = prm.get_double("Height");
        scale             = prm.get_double("Grid scale");
        p_p0              = prm.get_double("Pressure ratio p/p0");
      }
      prm.leave_subsection();
    }

    // @sect4{Muscle properties}

    struct MuscleProperties
    {
      double muscle_density;

      // Fibre properties
      double max_iso_stress_muscle; 
      double kappa_muscle;
      double max_strain_rate; 
      double muscle_fibre_orientation_x; 
      double muscle_fibre_orientation_y; 
      double muscle_fibre_orientation_z; 

      // Base material properties
      double max_iso_stress_basematerial;
      double muscle_basematerial_factor; 
      double muscle_basemat_c1;
      double muscle_basemat_c2;
      double muscle_basemat_c3;

      static void
      declare_parameters(ParameterHandler &prm);

      void
      parse_parameters(ParameterHandler &prm);
    };

    void MuscleProperties::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Materials");
      {
        prm.declare_entry("Muscle density", "1060",
                          Patterns::Double(),
                          "Muscle tissue density");

        // Fibre properties
        prm.declare_entry("Sigma naught muscle", "2.0e5",
                          Patterns::Double(),
                          "Muscle maximum isometric stress");

        prm.declare_entry("Bulk modulus muscle", "1.0e6",
                          Patterns::Double(),
                          "Muscle kappa value");

        prm.declare_entry("Max strain rate", "0.0",
                          Patterns::Double(),
                          "Maximum muscle fibre strain rate");
        
        prm.declare_entry("Muscle x component", "1.0",
                          Patterns::Double(0.0),
                          "Muscle fibre orientation x direction");

        prm.declare_entry("Muscle y component", "0.0",
                          Patterns::Double(0.0),
                          "Muscle fibre orientation y direction");

        prm.declare_entry("Muscle z component", "0.0",
                          Patterns::Double(0.0),
                          "Muscle fibre orientation z direction");
        
        // Base material properties
        prm.declare_entry("Sigma naught base material", "2.0e5",
                          Patterns::Double(),
                          "Base material maximum isometric stress");
        
        prm.declare_entry("Muscle base material factor", "1.0e1",
                          Patterns::Double(),
                          "Fictitious muscle base material multiplier");
        
        prm.declare_entry("Muscle base material constant 1", "0.0",
                          Patterns::Double(),
                          "Muscle base material constant 1");

        prm.declare_entry("Muscle base material constant 2", "0.0",
                          Patterns::Double(),
                          "Muscle base material constant 2");

        prm.declare_entry("Muscle base material constant 3", "0.0",
                          Patterns::Double(),
                          "Muscle base material constant 3");
      }
      prm.leave_subsection();
    }

    void MuscleProperties::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Materials");
      {
        muscle_density          = prm.get_double("Muscle density");

        max_iso_stress_muscle   = prm.get_double("Sigma naught muscle");
        kappa_muscle            = prm.get_double("Bulk modulus muscle");
        max_strain_rate         = prm.get_double("Max strain rate");
        muscle_fibre_orientation_x  = prm.get_double("Muscle x component"); 
        muscle_fibre_orientation_y  = prm.get_double("Muscle y component"); 
        muscle_fibre_orientation_z  = prm.get_double("Muscle z component");

        max_iso_stress_basematerial = prm.get_double("Sigma naught base material");
        muscle_basematerial_factor  = prm.get_double("Muscle base material factor");
        muscle_basemat_c1 = prm.get_double("Muscle base material constant 1"); 
        muscle_basemat_c2 = prm.get_double("Muscle base material constant 2"); 
        muscle_basemat_c3 = prm.get_double("Muscle base material constant 3"); 
      }
      prm.leave_subsection();
    }

    // @sect4{Activation}

    struct Activation
    {
        double activation_level;
        double activation_start;
        double activation_end;

        static void
        declare_parameters(ParameterHandler &prm);

        void
        parse_parameters(ParameterHandler &prm);
    };

    void Activation::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Activation");
      {
        prm.declare_entry("Activation level", "0.0",
                            Patterns::Double(),
                            "Maximum activation level");
        
        prm.declare_entry("Activation start", "0.0",
                            Patterns::Double(),
                            "Beginning of activation ramp");

        prm.declare_entry("Activation end", "1.0",
                            Patterns::Double(),
                            "End of activation ramp");
      }
      prm.leave_subsection();
    }

    void Activation::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Activation");
      {
        activation_level = prm.get_double("Activation level");
        activation_start = prm.get_double("Activation start");
        activation_end   = prm.get_double("Activation end");
      }
      prm.leave_subsection();
    }

    // @sect4{Linear solver}

    // Next, we choose both solver and preconditioner settings.  The use of an
    // effective preconditioner is critical to ensure convergence when a large
    // nonlinear motion occurs within a Newton increment.
    struct LinearSolver
    {
      std::string type_lin;
      double      tol_lin;
      double      max_iterations_lin;
      bool        use_static_condensation;
      std::string preconditioner_type;
      double      preconditioner_relaxation;

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);
    };

    void LinearSolver::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Linear solver");
      {
        prm.declare_entry("Solver type",
                          "CG",
                          Patterns::Selection("GMRES|CG|Direct"),
                          "Type of solver used to solve the linear system");

        prm.declare_entry("Residual",
                          "1e-6",
                          Patterns::Double(0.0),
                          "Linear solver residual (scaled by residual norm)");

        prm.declare_entry(
          "Max iteration multiplier",
          "1",
          Patterns::Double(0.0),
          "Linear solver iterations (multiples of the system matrix size)");

        prm.declare_entry("Use static condensation",
                          "true",
                          Patterns::Bool(),
                          "Solve the full block system or a reduced problem");

        prm.declare_entry("Preconditioner type",
                          "ssor",
                          Patterns::Selection("jacobi|ssor"),
                          "Type of preconditioner");

        prm.declare_entry("Preconditioner relaxation",
                          "0.65",
                          Patterns::Double(0.0),
                          "Preconditioner relaxation value");
      }
      prm.leave_subsection();
    }

    void LinearSolver::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Linear solver");
      {
        type_lin                  = prm.get("Solver type");
        tol_lin                   = prm.get_double("Residual");
        max_iterations_lin        = prm.get_double("Max iteration multiplier");
        use_static_condensation   = prm.get_bool("Use static condensation");
        preconditioner_type       = prm.get("Preconditioner type");
        preconditioner_relaxation = prm.get_double("Preconditioner relaxation");
      }
      prm.leave_subsection();
    }

    // @sect4{Nonlinear solver}

    // A Newton-Raphson scheme is used to solve the nonlinear system of
    // governing equations.  We now define the tolerances and the maximum number
    // of iterations for the Newton-Raphson nonlinear solver.
    struct NonlinearSolver
    {
      std::string  type_nonlinear_solver;
      unsigned int max_iterations_NR;
      double       tol_f;
      double       tol_u;

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);
    };

    void NonlinearSolver::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Nonlinear solver");
      {
        prm.declare_entry("Nonlinear solver type", "classicNewton",
                          Patterns::Selection("classicNewton|acceleratedNewton"),
                          "Type of nonlinear iteration");

        prm.declare_entry("Max iterations Newton-Raphson",
                          "10",
                          Patterns::Integer(0),
                          "Number of Newton-Raphson iterations allowed");

        prm.declare_entry("Tolerance force",
                          "1.0e-9",
                          Patterns::Double(0.0),
                          "Force residual tolerance");

        prm.declare_entry("Tolerance displacement",
                          "1.0e-6",
                          Patterns::Double(0.0),
                          "Displacement error tolerance");
      }
      prm.leave_subsection();
    }

    void NonlinearSolver::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Nonlinear solver");
      {
        type_nonlinear_solver = prm.get("Nonlinear solver type");
        max_iterations_NR = prm.get_integer("Max iterations Newton-Raphson");
        tol_f             = prm.get_double("Tolerance force");
        tol_u             = prm.get_double("Tolerance displacement");
      }
      prm.leave_subsection();
    }

    // @sect4{Time}

    // Set the timestep size $ \varDelta t $ and the simulation end-time.
    struct Time
    {
      double delta_t;
      double end_time;

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);
    };

    void Time::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Time");
      {
        prm.declare_entry("End time", "1", Patterns::Double(), "End time");

        prm.declare_entry("Time step size",
                          "0.1",
                          Patterns::Double(),
                          "Time step size");
      }
      prm.leave_subsection();
    }

    void Time::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Time");
      {
        end_time = prm.get_double("End time");
        delta_t  = prm.get_double("Time step size");
      }
      prm.leave_subsection();
    }

    // @sect4{PrescribedDisplacement}

    // Set the parameters for the prescribed displacement.
    struct PrescribedDisplacement
    {
      unsigned int pulling_face_id;
      double pull_time_start;
      double pull_time_end;
      double pull_strain;
      double pull_strain_rate;

      static void
      declare_parameters(ParameterHandler &prm);

      void
      parse_parameters(ParameterHandler &prm);
    };

    void PrescribedDisplacement::declare_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Prescribed displacement");
      {
        prm.declare_entry("Pulling face ID", "1",
                          Patterns::Integer(0,6),
                          "Boundary ID of face being pulled/pushed");
        
        prm.declare_entry("Pull time start", "0.0",
                          Patterns::Double(),
                          "Pulling start time");

        prm.declare_entry("Pull time end", "1.0",
                          Patterns::Double(),
                          "Pulling end time");
        
        prm.declare_entry("Pull strain", "0.0",
                          Patterns::Double(-1,10),
                          "Strain to which the body is being pulled");

        prm.declare_entry("Pull strain rate", "1.0",
                          Patterns::Double(0,100),
                          "Maximum pulling strain rate");
      }
      prm.leave_subsection();
    }

    void PrescribedDisplacement::parse_parameters(ParameterHandler &prm)
    {
      prm.enter_subsection("Prescribed displacement");
      {
        pulling_face_id  = prm.get_integer("Pulling face ID");
        pull_time_start  = prm.get_double("Pull time start");
        pull_time_end    = prm.get_double("Pull time end");
        pull_strain      = prm.get_double("Pull strain");
        pull_strain_rate = prm.get_double("Pull strain rate");
      }
      prm.leave_subsection();
    }

    // @sect4{All parameters}

    // Finally we consolidate all of the above structures into a single
    // container that holds all of our run-time selections.
    struct AllParameters : public FESystem,
                           public Geometry,
                           public MuscleProperties,
                           public Activation,
                           public LinearSolver,
                           public NonlinearSolver,
                           public Time,
                           public PrescribedDisplacement

    {
      AllParameters(const std::string &input_file);

      static void declare_parameters(ParameterHandler &prm);

      void parse_parameters(ParameterHandler &prm);

      // We decide to keep track of the parameter handler
      // object since we want to call prm.print_parameters()
      // after the constructor has been called and we need
      // to first set up the directories with the current
      // timestamp.
      ParameterHandler prm;
    };

    AllParameters::AllParameters(const std::string &input_file)
    {
      declare_parameters(prm);
      prm.parse_input(input_file);
      parse_parameters(prm);
    }

    void AllParameters::declare_parameters(ParameterHandler &prm)
    {
      FESystem::declare_parameters(prm);
      Geometry::declare_parameters(prm);
      MuscleProperties::declare_parameters(prm);
      Activation::declare_parameters(prm);
      LinearSolver::declare_parameters(prm);
      NonlinearSolver::declare_parameters(prm);
      Time::declare_parameters(prm);
      PrescribedDisplacement::declare_parameters(prm);
    }

    void AllParameters::parse_parameters(ParameterHandler &prm)
    {
      FESystem::parse_parameters(prm);
      Geometry::parse_parameters(prm);
      MuscleProperties::parse_parameters(prm);
      Activation::parse_parameters(prm);
      LinearSolver::parse_parameters(prm);
      NonlinearSolver::parse_parameters(prm);
      Time::parse_parameters(prm);
      PrescribedDisplacement::parse_parameters(prm);
    }
  } // namespace Parameters

  // @sect3{Time class}

  // A simple class to store time data. Its functioning is transparent so no
  // discussion is necessary. For simplicity we assume a constant time step
  // size.
  class Time
  {
  public:
    Time(const double time_end, const double delta_t)
      : timestep(0)
      , time_previous(0.0)
      , time_current(0.0)
      , time_end(time_end)
      , delta_t(delta_t)
    {}

    virtual ~Time() = default;

    double current() const
    {
      return time_current;
    }
    double previous() const
    {
      return time_previous;
    }
    double end() const
    {
      return time_end;
    }
    double get_delta_t() const
    {
      return delta_t;
    }
    unsigned int get_timestep() const
    {
      return timestep;
    }
    void increment()
    {
      time_previous = time_current;
      time_current += delta_t;
      ++timestep;
    }

  private:
    unsigned int timestep;
    double       time_previous;
    double       time_current;
    const double time_end;
    const double delta_t;
  };

  // @sect3{Muscle tissue within a three-field formulation}

  // Muscle can be described as a quasi-incompressible fibre-reinforced
  // material. Similar to the <code>Material_Compressible_Neo_Hook_Three_Field</code>
  // in step-44, the material here can be described by a strain-energy 
  // function $ \Psi = \Psi_{\text{iso}}(\overline{\mathbf{b}}) + 
  // \Psi_{\text{vol}}(\widetilde{J})$, where $\Psi_{iso}$ is composed of a 
  // fibre and a base material component $\Psi_{fibre}$ and $\Psi_{base}$, 
  // respectively.
  // 
  // In the current configuration, the fibre component takes the form
  // $\boldsymbol{\sigma} = \sigma_{Hill} \mathbf{a} \otimes \mathbf{a}$,
  // where $\sigma_{Hill} = \sigma_0 \left\{ a(t) \sigma_L(\lambda) 
  // \sigma_V(\epsilon) + \sigma_P(\lambda) \right\}$.
  //
  // In turn, the base material component is given by Yeoh SEF.
  template <int dim>
  class Muscle_Tissues_Three_Field
  {
  public:
    Muscle_Tissues_Three_Field(const std::string type_contraction,
                               const double max_iso_stress_muscle,
                               const double kappa_muscle,
                               const double max_strain_rate,
                               const double initial_fibre_orientation_x,
                               const double initial_fibre_orientation_y,
                               const double initial_fibre_orientation_z,
                               const double max_iso_stress_basematerial,
                               const double muscle_basematerial_factor,
                               const double muscle_basemat_c1,
                               const double muscle_basemat_c2,
                               const double muscle_basemat_c3)
      :
      type_of_contraction(type_contraction),
      sigma_naught_muscle(max_iso_stress_muscle),
      kappa_muscle(kappa_muscle),
      strain_rate_naught(max_strain_rate),
      initial_fibre_orientation({initial_fibre_orientation_x, 
                                 initial_fibre_orientation_y, 
                                 initial_fibre_orientation_z}),
      sigma_naught_basematerial(max_iso_stress_basematerial),
      s_base_muscle(muscle_basematerial_factor),
      c1_basematerial_muscle(muscle_basemat_c1),
      c2_basematerial_muscle(muscle_basemat_c2),
      c3_basematerial_muscle(muscle_basemat_c3),
      /* Physiological variables */
      stretch_bar(1.0),
      strain_rate_bar(0.0),
      fibre_time_activation(0.0),
      orientation(initial_fibre_orientation),
      /* Mechanical variables */
      det_F(1.0),
      p_tilde(0.0),
      J_tilde(1.0),
      b_bar(Physics::Elasticity::StandardTensors<dim>::I),
      trace_b_bar(3.0),
      delta_t(0.0)
      {
        Assert(kappa_muscle > 0, 
               ExcMessage("Bulk modulus must be positive!"));
        Assert(initial_fibre_orientation.norm() != 0, 
               ExcMessage("Initial fibre orientation must be a nonzero vector!"))
      }

    // We update the material model with various deformation dependent data
    // based on $F$ and the pressure $\widetilde{p}$ and dilatation
    // $\widetilde{J}$, and at the end of the function include a physical
    // check for internal consistency:
    void update_material_data(const Tensor<2, dim> &F,
                              const double          p_tilde_in,
                              const double          J_tilde_in,
                              const double          fibre_time_activation_in,
                              const Tensor<2, dim> &grad_velocity,
                              const double          delta_t_in)
    {
      // First compute the determinant of the deformation tensor
      // and stop the program immediately if it is negative.
      det_F                      = determinant(F);
      AssertThrow(det_F > 0, ExcInternalError());
      
      // Then, update the rest of the variables.
      p_tilde                    = p_tilde_in;
      J_tilde                    = J_tilde_in;
      fibre_time_activation      = fibre_time_activation_in;
      delta_t                    = delta_t_in;

      const Tensor<2, dim> F_bar = Physics::Elasticity::Kinematics::F_iso(F);
      b_bar                      = Physics::Elasticity::Kinematics::b(F_bar);
      trace_b_bar                = first_invariant(b_bar);
      
      // Update stretch_bar and strain_rate_bar using the current Newton iterate.
      const Tensor<2,dim> dev_symm_grad_velocity =
                        Physics::Elasticity::StandardTensors<dim>::dev_P * 
                        Physics::Elasticity::Kinematics::d(F,grad_velocity);
      orientation     = F_bar * initial_fibre_orientation;
      stretch_bar     = std::sqrt(orientation * orientation);
      strain_rate_bar = (1.0 / strain_rate_naught) * 
                         orientation * (dev_symm_grad_velocity * orientation) / stretch_bar;
    }

    // The second function determines the Kirchhoff stress $\boldsymbol{\tau}
    // = \boldsymbol{\tau}_{\textrm{iso}} + \boldsymbol{\tau}_{\textrm{vol}}$
    SymmetricTensor<2, dim> get_tau()
    {
      return get_tau_iso() + get_tau_vol();
    }

    // The fourth-order elasticity tensor in the spatial setting
    // $\mathfrak{c}$ is calculated from the SEF $\Psi$ as $ J
    // \mathfrak{c}_{ijkl} = F_{iA} F_{jB} \mathfrak{C}_{ABCD} F_{kC} F_{lD}$
    // where $ \mathfrak{C} = 4 \frac{\partial^2 \Psi(\mathbf{C})}{\partial
    // \mathbf{C} \partial \mathbf{C}}$
    SymmetricTensor<4, dim> get_Jc() const
    {
      return get_Jc_vol() + get_Jc_iso();
    }

    // Derivative of the volumetric free energy with respect to
    // $\widetilde{J}$ return $\frac{\partial
    // \Psi_{\text{vol}}(\widetilde{J})}{\partial \widetilde{J}}$
    double get_dPsi_vol_dJ() const
    {
      return (kappa_muscle / 2.0) * (J_tilde - 1.0 / J_tilde);
    }

    // Second derivative of the volumetric free energy wrt $\widetilde{J}$. We
    // need the following computation explicitly in the tangent so we make it
    // public.  We calculate $\frac{\partial^2
    // \Psi_{\textrm{vol}}(\widetilde{J})}{\partial \widetilde{J} \partial
    // \widetilde{J}}$
    double get_d2Psi_vol_dJ2() const
    {
      return ((kappa_muscle / 2.0) * (1.0 + 1.0 / (J_tilde * J_tilde)));
    }

    // The next few functions return various data that we choose to store with
    // the material:
    double get_det_F() const
    {
      return det_F;
    }

    double get_p_tilde() const
    {
      return p_tilde;
    }

    double get_J_tilde() const
    {
      return J_tilde;
    }

    double get_stretch() const
    {
      return std::pow(det_F, 1/3) * stretch_bar;
    }

    Tensor<1, dim> get_orientation() const
    {
      return std::pow(det_F, 1/3) * orientation;
    }

  protected:
    // Define constitutive model parameters
    const std::string       type_of_contraction;
    const double            sigma_naught_muscle;
    const double            kappa_muscle;
    const double            strain_rate_naught;
    const Tensor<1, dim>    initial_fibre_orientation;
    const double            sigma_naught_basematerial;
    const double            s_base_muscle;
    const double            c1_basematerial_muscle;
    const double            c2_basematerial_muscle;
    const double            c3_basematerial_muscle;

    // Define physiological variables needed to evaluate the 
    // constitutive models
    double                  stretch_bar;
    double                  strain_rate_bar;
    double                  fibre_time_activation;
    Tensor<1, dim>          orientation;

    // Define mechanical variables
    double                  det_F;
    double                  p_tilde;
    double                  J_tilde;
    SymmetricTensor<2, dim> b_bar;
    double                  trace_b_bar;
    double                  delta_t;

    // The following functions are used internally in determining the result
    // of some of the public functions above. The first one determines the
    // volumetric Kirchhoff stress $\boldsymbol{\tau}_{\textrm{vol}}$:
    SymmetricTensor<2, dim> get_tau_vol() const
    {
      return p_tilde * det_F * Physics::Elasticity::StandardTensors<dim>::I;
    }

    // Next, determine the isochoric Kirchhoff stress
    // $\boldsymbol{\tau}_{\textrm{iso}} =
    // \mathcal{P}:\overline{\boldsymbol{\tau}}$:
    SymmetricTensor<2, dim> get_tau_iso() const
    {
      return Physics::Elasticity::StandardTensors<dim>::dev_P * get_tau_bar();
    }

    // Just like the SEF of the system, the isochoric
    // Kirchhoff stress is made of two parts:
    // a fibre component and a base material component.
    // In particular, we decide to subdivide the
    // fibre component into its active and passive
    // components.
    SymmetricTensor<2,dim> get_tau_iso_muscle_active()
    {
      return Physics::Elasticity::StandardTensors<dim>::dev_P * get_tau_muscle_active_bar();
    }

    SymmetricTensor<2,dim> get_tau_iso_muscle_passive()
    {
      return Physics::Elasticity::StandardTensors<dim>::dev_P * get_tau_muscle_passive_bar();
    }

    SymmetricTensor<2,dim> get_tau_iso_muscle_basematerial()
    {
      return Physics::Elasticity::StandardTensors<dim>::dev_P * get_tau_muscle_basematerial_bar();
    }

    // Then, determine the fictitious Kirchhoff stress
    // $\overline{\boldsymbol{\tau}}$:
    SymmetricTensor<2, dim> get_tau_bar() const
    {
      return get_tau_muscle_active_bar() + get_tau_muscle_passive_bar() + get_tau_muscle_basematerial_bar();
    }

    // Determine the contributions from active and passive muscle fibres,
    // as well as from the base material:
    SymmetricTensor<2, dim> get_tau_muscle_active_bar() const
    {
      const double active_level = fibre_time_activation;
      double sigma_active_muscle_fibre = 0.0;
      
      if (type_of_contraction == "quasi-static")
        sigma_active_muscle_fibre = sigma_naught_muscle * active_level * get_length_stress() * 1.0;
      else if (type_of_contraction == "dynamic")
        sigma_active_muscle_fibre = sigma_naught_muscle * active_level * get_length_stress() * get_strain_rate_stress();
      
      return (1.0 / std::pow(stretch_bar, 2)) * sigma_active_muscle_fibre *
               symmetrize(outer_product(orientation,orientation));
    }

    SymmetricTensor<2, dim> get_tau_muscle_passive_bar() const
    {
      const double sigma_passive_muscle_fibre = sigma_naught_muscle * get_passive_stress();
      return (1.0 / std::pow(stretch_bar,2)) * sigma_passive_muscle_fibre * 
                symmetrize(outer_product(orientation,orientation));
    }

    SymmetricTensor<2, dim> get_tau_muscle_basematerial_bar() const
    {
      return 2 * s_base_muscle * sigma_naught_basematerial * 
            (3 * c3_basematerial_muscle * std::pow(trace_b_bar - 3,2)
           + 2 * c2_basematerial_muscle * (trace_b_bar - 3) + c1_basematerial_muscle) * b_bar;
    }

    // Calculate the volumetric part of the tangent $J
    // \mathfrak{c}_\textrm{vol}$:
    SymmetricTensor<4, dim> get_Jc_vol() const
    {
      return p_tilde * det_F *
             (Physics::Elasticity::StandardTensors<dim>::IxI -
              (2.0 * Physics::Elasticity::StandardTensors<dim>::S));
    }

    // Calculate the isochoric part of the tangent $J
    // \mathfrak{c}_\textrm{iso}$:
    SymmetricTensor<4, dim> get_Jc_iso() const
    {
      const SymmetricTensor<2, dim> tau_bar = get_tau_bar();
      const SymmetricTensor<2, dim> tau_iso = get_tau_iso();
      const SymmetricTensor<4, dim> tau_iso_x_I =
        outer_product(tau_iso, Physics::Elasticity::StandardTensors<dim>::I);
      const SymmetricTensor<4, dim> I_x_tau_iso =
        outer_product(Physics::Elasticity::StandardTensors<dim>::I, tau_iso);
      const SymmetricTensor<4, dim> c_bar = get_c_bar();

      return (2.0 / dim) * trace(tau_bar) *
               Physics::Elasticity::StandardTensors<dim>::dev_P -
             (2.0 / dim) * (tau_iso_x_I + I_x_tau_iso) +
             Physics::Elasticity::StandardTensors<dim>::dev_P * c_bar *
               Physics::Elasticity::StandardTensors<dim>::dev_P;
    }

    // Calculate the fictitious elasticity tensor $\overline{\mathfrak{c}}$.
    // Note that because the material is no longer Neo-Hookean as in step-44,
    // these tensors are no longer zero. Moreover, care must be taken when
    // linearizing the elasticity equations as the velocity variable, which
    // is only present in dynamic simulations, introduces additional terms
    // to the tangent matrix.
    SymmetricTensor<4, dim> get_c_bar() const
    {
      return get_c_muscle_active_bar() + get_c_muscle_passive_bar() + get_c_muscle_basematerial_bar();
    }

    SymmetricTensor<4, dim> get_c_muscle_active_bar() const
    {
      const double activation_level = fibre_time_activation;
      const SymmetricTensor<2, dim> 
        orientation_x_orientation = symmetrize(outer_product(orientation,orientation));

      double first_term = 0.0, second_term = 0.0, third_term = 0.0;

      if (type_of_contraction == "quasi-static")
      {
        first_term  = - (2 / std::pow( stretch_bar ,4))
                    * sigma_naught_muscle
                    * activation_level
                    * get_length_stress()
                    * 1.0;

        second_term = (1 / std::pow( stretch_bar ,3))
                    * sigma_naught_muscle
                    * activation_level
                    * get_dlength_stress_dstretch() * 1.0;
        
        third_term = 0.0;
      }
      else if (type_of_contraction == "dynamic")
      {
        first_term  = - (2 / std::pow( stretch_bar ,4))
                      * sigma_naught_muscle
                      * activation_level
                      * get_length_stress()
                      * get_strain_rate_stress();
        
        second_term =   (1 / std::pow( stretch_bar ,3))
                      * sigma_naught_muscle
                      * activation_level
                      * get_dlength_stress_dstretch() * get_strain_rate_stress();
        
        third_term  =   (1 / std::pow( stretch_bar ,3))
                      * sigma_naught_muscle
                      * activation_level
                      * get_length_stress() * get_dstrain_rate_stress_dstrain_rate() * (1.0 / delta_t);
      }

      return (first_term + second_term + third_term) * 
        outer_product(orientation_x_orientation,orientation_x_orientation);
    }

    SymmetricTensor<4, dim> get_c_muscle_passive_bar() const
    {
      const SymmetricTensor<2, dim> 
        orientation_x_orientation = symmetrize(outer_product(orientation,orientation));
      
      const double first_term  = - (2 / std::pow( stretch_bar ,4))
                               * sigma_naught_muscle
                               * get_passive_stress();
      const double second_term =   (1 / std::pow( stretch_bar ,3))
                                * sigma_naught_muscle
                                * get_dpassive_stress_dstretch();

      return (first_term + second_term) * 
        outer_product(orientation_x_orientation,orientation_x_orientation);
    }

    SymmetricTensor<4, dim> get_c_muscle_basematerial_bar() const
    {
      return 4 * sigma_naught_basematerial * s_base_muscle * 
            (6 * c3_basematerial_muscle * (trace_b_bar - 3)
           + 2 * c2_basematerial_muscle) * outer_product(b_bar,b_bar);
    }

    // Finally, we define the stress relationships of muscle,
    // along with their derivatives. These are the "stress"
    // versions of the traditional force-length and 
    // force-velocity relationships used in the traditional 
    // Hill model of muscle.
    double get_length_stress() const
    {
      if (stretch_bar >= 0.4 && stretch_bar <= 1.75)
        return  (0.642587074375392 * sin(1.290128342448810 * stretch_bar + 0.629168420414746)
                +0.325979591577056 * sin(5.308969899884336 * stretch_bar + -4.520101562237307)
                +0.328204247867325 * sin(6.744187042136006 * stretch_bar + 1.689155892259429)
                +0.015388902741327 * sin(19.823676877725276 * stretch_bar + -7.386155292116579)
                +0.139240359517525 * sin(8.038287396059996 * stretch_bar + 2.543022326676525)
                +0.001801867529599 * sin(32.237736486095052 * stretch_bar + -6.454098315528945)
                +0.012560837549867 * sin(23.117614057963024 * stretch_bar + -2.643346778503341));
      else
        return 0.0;
    }

    double get_strain_rate_stress() const
    {
      if (strain_rate_bar < -1.2)
        return 0.0;
      else if (strain_rate_bar >= -1.2 && strain_rate_bar < -0.25)
        return 0.25792669408341773*std::pow(strain_rate_bar+1.2,3)
            + 0.14317485143460784*std::pow(strain_rate_bar+1.2,2)
            + 0.0*(strain_rate_bar+1.2)
            + 0.0;
      else if (strain_rate_bar >= -0.25 && strain_rate_bar < 0.0)
        return 29.825565394304522*std::pow(strain_rate_bar+0.25,3)
            + -0.9435495605479662*std::pow(strain_rate_bar+0.25,2)
            + 0.9703687419567255*(strain_rate_bar+0.25)
            + 0.3503552027590582;
      else if (strain_rate_bar >= 0.0 && strain_rate_bar < 0.05)
        return -3165.6847983144276*std::pow(strain_rate_bar-0.0,3)
            + 186.19612494819665*std::pow(strain_rate_bar-0.0,2)
            + 6.090887473114851*(strain_rate_bar-0.0)
            + 1.0;
      else if (strain_rate_bar >= 0.05 && strain_rate_bar < 0.75)
        return 0.6882206253246714*std::pow(strain_rate_bar-0.05,3)
            + -1.413963071288272*std::pow(strain_rate_bar-0.05,2)
            + 0.9678639805763023*(strain_rate_bar-0.05)
            + 1.3743240862369306;
      else// if (strain_rate_bar >= 0.75)
          return 1.5950466421954534;
    }

    double get_passive_stress() const
    {
      if (stretch_bar >= 0 && stretch_bar <= 1.)
        return 0.0;
      else if (stretch_bar > 1. && stretch_bar <= 1.25)
        return (2.353844827629192 * pow((stretch_bar - 1),2) + 0.0 * (stretch_bar - 1) + 0.0);
      else if (stretch_bar > 1.25 && stretch_bar <= 1.5)
        return (3.436356700507747 * pow((stretch_bar - 1.25),2) + 1.176922413814596 * (stretch_bar - 1.25) + 0.1471153017268245);
      else if (stretch_bar > 1.5 && stretch_bar <= 1.65)
        return (0.4274082856676522 * pow((stretch_bar - 1.5),2) + 2.8951007640684696 * (stretch_bar - 1.5) + 0.6561181989622077);
      else if (stretch_bar > 1.65)
        return (3.023323249768765 * (stretch_bar - 1.65) + 1.1);
      else
        return 0.0;
    }

    double get_dlength_stress_dstretch() const
    {
      if (stretch_bar >= 0.4 && stretch_bar <= 1.75)
        return  (0.829019797142955 * cos(1.290128342448810 * stretch_bar + 0.629168420414746)
                +1.730615839659180 * cos(5.308969899884336 * stretch_bar + -4.520101562237307)
                +2.213470835640807 * cos(6.744187042136006 * stretch_bar + 1.689155892259429)
                +0.305064635446807 * cos(19.823676877725276 * stretch_bar + -7.386155292116579)
                +1.119254026932584 * cos(8.038287396059996 * stretch_bar + 2.543022326676525)
                +0.058088130602064 * cos(32.237736486095052 * stretch_bar + -6.454098315528945)
                +0.290376594722595 * cos(23.117614057963024 * stretch_bar + -2.643346778503341));
      else
        return 0.0;
    }

    double get_dstrain_rate_stress_dstrain_rate() const
    {
      if (strain_rate_bar < -1.2)
        return  0.0;
      else if (strain_rate_bar >= -1.2 && strain_rate_bar < -0.25)
        return  (3*0.25792669408341773*std::pow(strain_rate_bar+1.2,2)
                            + 2*0.14317485143460784*(strain_rate_bar+1.2) + 0.0);
      else if (strain_rate_bar >= -0.25 && strain_rate_bar < 0.0)
        return  (3*29.825565394304522*std::pow(strain_rate_bar+0.25,2)
                            + 2*-0.9435495605479662*(strain_rate_bar+0.25) + 0.9703687419567255);
      else if (strain_rate_bar >= 0.0 && strain_rate_bar < 0.05)
        return  (3*-3165.6847983144276*std::pow(strain_rate_bar-0.0,2)
                            + 2*186.19612494819665*(strain_rate_bar-0.0) + 6.090887473114851);
      else if (strain_rate_bar >= 0.05 && strain_rate_bar < 0.75)
        return  (3*0.6882206253246714*std::pow(strain_rate_bar-0.05,2)
                            + 2*-1.413963071288272*(strain_rate_bar-0.05) + 0.9678639805763023); 
      else// if (strain_rate_bar >= 0.75)
        return  0.0;
    }

    double get_dpassive_stress_dstretch() const
    {
      if (stretch_bar >= 0 && stretch_bar <= 1.)
        return 0.0;
      else if (stretch_bar > 1. && stretch_bar <= 1.25)
        return (2 * 2.353844827629192 * (stretch_bar - 1));
      else if (stretch_bar > 1.25 && stretch_bar <= 1.5)
        return (2 * 3.436356700507747 * (stretch_bar - 1.25) + 1.176922413814596);
      else if (stretch_bar > 1.5 && stretch_bar <= 1.65)
        return (2 * 0.4274082856676522 * (stretch_bar - 1.5) + 2.8951007640684696);
      else if (stretch_bar > 1.65)
        return (3.023323249768765);
      else
        return 0.0;
    }
  };

  // @sect3{Quadrature point history}

  // As seen in step-18, the <code> PointHistory </code> class offers a method
  // for storing data at the quadrature points.  Here each quadrature point
  // holds a pointer to a material description.  Thus, different material models
  // can be used in different regions of the domain.  Among other data, we
  // choose to store the Kirchhoff stress $\boldsymbol{\tau}$ and the tangent
  // $J\mathfrak{c}$ for the quadrature points.
  template <int dim>
  class PointHistory
  {
  public:
    PointHistory()
      : F_inv(Physics::Elasticity::StandardTensors<dim>::I)
      , tau(SymmetricTensor<2, dim>())
      , d2Psi_vol_dJ2(0.0)
      , dPsi_vol_dJ(0.0)
      , Jc(SymmetricTensor<4, dim>())
      , displacement(Tensor<1, dim>())
      , displacement_previous(Tensor<1, dim>())
      , velocity_previous((Tensor<1, dim>()))
      , grad_velocity((Tensor<2, dim>()))
      , F_previous(Physics::Elasticity::StandardTensors<dim>::I)
    {}

    virtual ~PointHistory() = default;

    // The first function is used to create a material object and to
    // initialize all tensors correctly: The second one updates the stored
    // values and stresses based on the current deformation measure
    // $\textrm{Grad}\mathbf{u}_{\textrm{n}}$, pressure $\widetilde{p}$ and
    // dilation $\widetilde{J}$ field values.
    void setup_lqp(const Parameters::AllParameters &parameters)
    {
      material =
        std::make_shared<Muscle_Tissues_Three_Field<dim>>(
          parameters.type_of_simulation,
          parameters.max_iso_stress_muscle,
          parameters.kappa_muscle,
          parameters.max_strain_rate,
          parameters.muscle_fibre_orientation_x,
          parameters.muscle_fibre_orientation_y,
          parameters.muscle_fibre_orientation_z,
          parameters.max_iso_stress_basematerial,
          parameters.muscle_basematerial_factor,
          parameters.muscle_basemat_c1,
          parameters.muscle_basemat_c2,
          parameters.muscle_basemat_c3);
      
      update_values(Tensor<1,dim>(),    /*Displacement*/
                    Tensor<2,dim>(),    /*Gradient of displacement*/
                    0.0,                /*Pressure*/
                    1.0,                /*Dilation*/
                    0.0,                /*Activation*/
                    parameters.delta_t  /*Time step size*/);
    }

    // To this end, we calculate the deformation gradient $\mathbf{F}$ from
    // the displacement gradient $\textrm{Grad}\ \mathbf{u}$, i.e.
    // $\mathbf{F}(\mathbf{u}) = \mathbf{I} + \textrm{Grad}\ \mathbf{u}$ and
    // then let the material model associated with this quadrature point
    // update itself. When computing the deformation gradient, we have to take
    // care with which data types we compare the sum $\mathbf{I} +
    // \textrm{Grad}\ \mathbf{u}$: Since $I$ has data type SymmetricTensor,
    // just writing <code>I + Grad_u_n</code> would convert the second
    // argument to a symmetric tensor, perform the sum, and then cast the
    // result to a Tensor (i.e., the type of a possibly nonsymmetric
    // tensor). However, since <code>Grad_u_n</code> is nonsymmetric in
    // general, the conversion to SymmetricTensor will fail. We can avoid this
    // back and forth by converting $I$ to Tensor first, and then performing
    // the addition as between nonsymmetric tensors:
    void update_values(const Tensor<1, dim> &u_n,
                       const Tensor<2, dim> &Grad_u_n,
                       const double          p_tilde,
                       const double          J_tilde,
                       const double          fibre_activation_time,
                       const double          delta_t)
    {
      const Tensor<2, dim> F = Physics::Elasticity::Kinematics::F(Grad_u_n);
      grad_velocity = (F - F_previous) / delta_t;
      material->update_material_data(F, 
                                     p_tilde, 
                                     J_tilde,
                                     fibre_activation_time,
                                     grad_velocity,
                                     delta_t);
      displacement = u_n;

      // The material has been updated so we now calculate the Kirchhoff
      // stress $\mathbf{\tau}$, the tangent $J\mathfrak{c}$ and the first and
      // second derivatives of the volumetric free energy.
      //
      // We also store the inverse of the deformation gradient since we
      // frequently use it:
      F_inv         = invert(F);
      tau           = material->get_tau();
      Jc            = material->get_Jc();
      dPsi_vol_dJ   = material->get_dPsi_vol_dJ();
      d2Psi_vol_dJ2 = material->get_d2Psi_vol_dJ2();
    }

    void update_values_timestep(Time &time_object)
    {
      velocity_previous     = (displacement - displacement_previous) / time_object.get_delta_t();
      displacement_previous = displacement;
      // We also update the F_previous variable, needed to compute the current grad_velocity;
      F_previous            = invert(F_inv);
    }

    // We offer an interface to retrieve certain data.  Here are the kinematic
    // variables:
    double get_J_tilde() const
    {
      return material->get_J_tilde();
    }

    double get_det_F() const
    {
      return material->get_det_F();
    }

    double get_stretch() const
    {
      return material->get_stretch();
    }

    Tensor<1, dim> get_orientation() const
    {
      return material->get_orientation();
    }

    const Tensor<2, dim> &get_F_inv() const
    {
      return F_inv;
    }

    // (and for the dynamic case)
    const Tensor<1, dim> &get_displacement() const
    {
      return displacement;
    }

    const Tensor<1, dim> &get_displacement_previous() const
    {
      return displacement_previous;
    }

    const Tensor<1, dim> &get_velocity_previous() const
    {
      return velocity_previous;
    }

    // ...and the kinetic variables.  These are used in the material and
    // global tangent matrix and residual assembly operations:
    double get_p_tilde() const
    {
      return material->get_p_tilde();
    }

    const SymmetricTensor<2, dim> &get_tau() const
    {
      return tau;
    }

    double get_dPsi_vol_dJ() const
    {
      return dPsi_vol_dJ;
    }

    double get_d2Psi_vol_dJ2() const
    {
      return d2Psi_vol_dJ2;
    }

    // And finally the tangent:
    const SymmetricTensor<4, dim> &get_Jc() const
    {
      return Jc;
    }

    // In terms of member functions, this class stores for the quadrature
    // point it represents a copy of a material type in case different
    // materials are used in different regions of the domain, as well as the
    // inverse of the deformation gradient...
  private:
    std::shared_ptr<Muscle_Tissues_Three_Field<dim>> material;

    Tensor<2, dim> F_inv;

    // ... and stress-type variables along with the tangent $J\mathfrak{c}$:
    SymmetricTensor<2, dim> tau;
    double                  d2Psi_vol_dJ2;
    double                  dPsi_vol_dJ;

    SymmetricTensor<4, dim> Jc;

    // Dynamic variables
    Tensor<1, dim> displacement;
    Tensor<1, dim> displacement_previous;
    Tensor<1, dim> velocity_previous;
    Tensor<2, dim> grad_velocity; // This variable is updated at each Newton iteration
    Tensor<2, dim> F_previous; // This variable is updated at each time step
  };

  // @sect3{PrescribedDisplacement and IncrementalDisplacement classes}
  
  // We first implement the PrescribedDisplacement class which
  // which keeps track of the Dirichlet boundary condition for
  // the whole problem. Note however that, because we are solving
  // a series of Newton increments, THIS IS NOT THE BOUNDARY
  // CONDITION THAT NEEDS TO BE IMPLEMENTED. This is done in
  // the IncrementalDisplacement class.
  template <int dim>
  class PrescribedDisplacement
  {
  public:
    PrescribedDisplacement(const double pull_start,
                           const double pull_end,
                           const double pull_strain,
                           const double pull_strain_rate,
                           const double muscle_length)
      :
      pull_start(pull_start),
      pull_end(pull_end),
      pull_strain(pull_strain),
      pull_strain_rate(pull_strain_rate),
      muscle_length(muscle_length)
    {
      // Verify that pull_strain_rate is fast enough given the
      // start and end times
      const double min_strain_rate = 
        std::abs(pull_strain * muscle_length / (pull_end - pull_start));
      const std::string error_message = 
        "Pull strain rate, currently set to " + std::to_string(pull_strain_rate) + 
        ", must be set to at least " + std::to_string(min_strain_rate) + ".";
      AssertThrow(pull_strain_rate >= min_strain_rate, ExcMessage(error_message));
    }

    double displacement(const double t)
    {
      double value = 0.0;

      // Compute t0, t1
      const double L0 = muscle_length;
      const double L1 = muscle_length * (1 + pull_strain);
      const double Lmid = (L0 + L1) / 2.0;
      const double tmid = (pull_start + pull_end) / 2.0;

      double t0, t1;
      if (pull_strain >= 0)
      {
        t0 = (L0 - Lmid) / pull_strain_rate + tmid;
        t1 = (L1 - Lmid) / pull_strain_rate + tmid;
      }
      else
      {
        t0 = (L1 - Lmid) / pull_strain_rate + tmid;
        t1 = (L0 - Lmid) / pull_strain_rate + tmid;
      }
      
      int sign_pull = 0;
      if (pull_strain != 0)
        sign_pull = (pull_strain > 0) ? 1 : -1;

      if (t <= t0)
        value = 0.0;
      else if (t > t0 && t < t1)
        value = sign_pull * pull_strain_rate * (t - tmid) + Lmid - muscle_length;
      else if (t >= t1)
        value = pull_strain * muscle_length;
      
      return value;
    }
    
    private:
      const double pull_start;
      const double pull_end;
      const double pull_strain;
      const double pull_strain_rate;
      const double muscle_length;
  };


  // An important note in this class: because this will be fed into
  // the VectorTools::interpolate_boundary_values function, it MUST
  // inherit Function<dim>. Moreover, to avoid the error message
  // "Dimension 5 not equal to 3" when running in Debug mode, we 
  // must intialize the Function<dim> element using the same number
  // of components that the finite element will have. In the Solid
  // class below, this number is given by n_components = dim + 2. 
  // However, because at this juncture we have not implement Solid 
  // yet, we must hard code this. Note also that, although this might
  // sound like we are about to implement a boundary condition
  // for all three variables (i.e. including pressure and dilation),
  // this discrepancy will be solved when calling component_mask.
  template <int dim>
  class IncrementalDisplacement : public Function<dim>
  {
  public:
    IncrementalDisplacement(const double u_dir_n, const double u_dir_n_1)
        :
        Function<dim>(dim + 2),
        u_dir_n(u_dir_n),
        u_dir_n_1(u_dir_n_1)
    {}

    virtual double value(const Point<dim> &point,
                          const unsigned int component = 0) const override;
  
  private:
    const double u_dir_n, u_dir_n_1;
  };

  template <>
  double IncrementalDisplacement<3>::value(const Point<3> &/*point*/,
                                           const unsigned int component) const
  {
    return (component == 0) ? (u_dir_n - u_dir_n_1) : 0.0;
  }

  // Activation profile
  class Activation
  {
  public:
    Activation(const double activation_start,
               const double activation_end,
               const double activation_level)
      :
      activation_start(activation_start),
      activation_end(activation_end),
      activation_level(activation_level)
    {}

    double value(const double t) const
    {
      double a = 0;

      if (activation_level != 0)
      {
        if (t <= activation_start)
          a = 0.0;
        else if (t > activation_start && t < activation_end)
          a = (activation_level/100) / (activation_end - activation_start) * (t - activation_start);
        else if (t >= activation_end)
          a = (activation_level/100);
      }

      return a;
    }
  
  private:
    const double activation_start;
    const double activation_end;
    const double activation_level;
  };


  // @sect3{Quasi-static quasi-incompressible finite-strain solid}

  // The Solid class is the central class in that it represents the problem at
  // hand. It follows the usual scheme in that all it really has is a
  // constructor, destructor and a <code>run()</code> function that dispatches
  // all the work to private functions of this class:
  template <int dim>
  class Solid
  {
  public:
    Solid(const std::string &input_file);

    void run();

  private:
    // In the private section of this class, we first forward declare a number
    // of objects that are used in parallelizing work using the WorkStream
    // object (see the @ref threads module for more information on this).
    //
    // We declare such structures for the computation of tangent (stiffness)
    // matrix and right hand side vector, static condensation, and for updating
    // quadrature points:
    struct PerTaskData_ASM;
    struct ScratchData_ASM;

    struct PerTaskData_SC;
    struct ScratchData_SC;

    struct PerTaskData_UQPH;
    struct ScratchData_UQPH;

    // and at each time step:
    struct PerTaskData_TIMESTEP;
    struct ScratchData_TIMESTEP;

    // We start the collection of member functions with one that builds the
    // grid:
    void make_grid();

    // Obtain all boundary IDs
    void determine_boundary_ids();

    // Set up the finite element system to be solved:
    void system_setup();

    void determine_component_extractors();

    // Create Dirichlet constraints for the incremental displacement field:
    void make_constraints(const int it_nr);

    // Several functions to assemble the system and right hand side matrices
    // using multithreading. Each of them comes as a wrapper function, one
    // that is executed to do the work in the WorkStream model on one cell,
    // and one that copies the work done on this one cell into the global
    // object that represents it:
    void assemble_system();

    void assemble_system_one_cell(
      const typename DoFHandler<dim>::active_cell_iterator &cell,
      ScratchData_ASM &                                     scratch,
      PerTaskData_ASM &                                     data) const;

    // And similar to perform global static condensation:
    void assemble_sc();

    void assemble_sc_one_cell(
      const typename DoFHandler<dim>::active_cell_iterator &cell,
      ScratchData_SC &                                      scratch,
      PerTaskData_SC &                                      data);

    void copy_local_to_global_sc(const PerTaskData_SC &data);

    // Create and update the quadrature points. Here, no data needs to be
    // copied into a global object, so the copy_local_to_global function is
    // empty:
    void setup_qph();

    void update_qph_incremental(const BlockVector<double> &solution_delta);

    void update_qph_incremental_one_cell(
      const typename DoFHandler<dim>::active_cell_iterator &cell,
      ScratchData_UQPH &                                    scratch,
      PerTaskData_UQPH &                                    data);

    void copy_local_to_global_UQPH(const PerTaskData_UQPH & /*data*/)
    {}

    void update_timestep();
        void update_timestep_one_cell(
            const typename DoFHandler<dim>::active_cell_iterator& cell,
            ScratchData_TIMESTEP&                                 scratch,
            PerTaskData_TIMESTEP&                                 data
        );

    void copy_local_to_global_timestep(const PerTaskData_TIMESTEP& /*data*/)
    {}

    // Solve for the displacement using a Newton-Raphson method. We break this
    // function into the nonlinear loop and the function that solves the
    // linearized Newton-Raphson step:
    void solve_nonlinear_timestep(BlockVector<double> &solution_delta);

    std::pair<unsigned int, double>
    solve_linear_system(BlockVector<double> &newton_update);

    // Solution retrieval as well as post-processing and writing data to file:
    BlockVector<double>
    get_total_solution(const BlockVector<double> &solution_delta) const;

    void output_results() const;
    void output_along_fibre_stretch() const;

    // Finally, some member variables that describe the current state: A
    // collection of the parameters used to describe the problem setup...
    Parameters::AllParameters parameters;

    // ...the volume of the reference configuration...
    double vol_reference;

    // ...and description of the geometry on which the problem is solved:
    Triangulation<dim> triangulation;
    std::vector<unsigned int> list_of_boundary_ids;

    // Also, keep track of the current time and the time spent evaluating
    // certain functions
    Time                time;
    mutable TimerOutput timer;

    // Create pulling profile
    PrescribedDisplacement<dim> u_dir;

    // Create activation profile
    Activation activation_function;

    // A storage object for quadrature point information. As opposed to
    // step-18, deal.II's native quadrature point data manager is employed
    // here.
    CellDataStorage<typename Triangulation<dim>::cell_iterator,
                    PointHistory<dim>>
      quadrature_point_history;

    // A description of the finite-element system including the displacement
    // polynomial degree, the degree-of-freedom handler, number of DoFs per
    // cell and the extractor objects used to retrieve information from the
    // solution vectors:
    const unsigned int               degree;
    const FESystem<dim>              fe;
    DoFHandler<dim>                  dof_handler;
    const unsigned int               dofs_per_cell;
    const FEValuesExtractors::Vector u_fe;
    const FEValuesExtractors::Scalar p_fe;
    const FEValuesExtractors::Scalar J_fe;

    // Description of how the block-system is arranged. There are 3 blocks,
    // the first contains a vector DOF $\mathbf{u}$ while the other two
    // describe scalar DOFs, $\widetilde{p}$ and $\widetilde{J}$.
    static const unsigned int n_blocks          = 3;
    static const unsigned int n_components      = dim + 2;
    static const unsigned int first_u_component = 0;
    static const unsigned int p_component       = dim;
    static const unsigned int J_component       = dim + 1;

    enum
    {
      u_dof = 0,
      p_dof = 1,
      J_dof = 2
    };

    std::vector<types::global_dof_index> dofs_per_block;
    std::vector<types::global_dof_index> element_indices_u;
    std::vector<types::global_dof_index> element_indices_p;
    std::vector<types::global_dof_index> element_indices_J;

    // Rules for Gauss-quadrature on both the cell and faces. The number of
    // quadrature points on both cells and faces is recorded.
    const QGauss<dim>     qf_cell;
    const QGauss<dim - 1> qf_face;
    const unsigned int    n_q_points;
    const unsigned int    n_q_points_f;

    // Objects that store the converged solution and right-hand side vectors,
    // as well as the tangent matrix. There is an AffineConstraints object used
    // to keep track of constraints.  We make use of a sparsity pattern
    // designed for a block system.
    AffineConstraints<double> constraints;
    BlockSparsityPattern      sparsity_pattern;
    BlockSparseMatrix<double> tangent_matrix;
    BlockVector<double>       system_rhs;
    BlockVector<double>       solution_n;

    // Then define a number of variables to store norms and update norms and
    // normalization factors.
    struct Errors
    {
      Errors()
        : norm(1.0)
        , u(1.0)
        , p(1.0)
        , J(1.0)
      {}

      void reset()
      {
        norm = 1.0;
        u    = 1.0;
        p    = 1.0;
        J    = 1.0;
      }
      void normalize(const Errors &rhs)
      {
        if (rhs.norm != 0.0)
          norm /= rhs.norm;
        if (rhs.u != 0.0)
          u /= rhs.u;
        if (rhs.p != 0.0)
          p /= rhs.p;
        if (rhs.J != 0.0)
          J /= rhs.J;
      }

      double norm, u, p, J;
    };

    Errors error_residual, error_residual_0, error_residual_norm, error_update,
      error_update_0, error_update_norm;

    // Methods to calculate error measures
    void get_error_residual(Errors &error_residual);

    void get_error_update(const BlockVector<double> &newton_update,
                          Errors &                   error_update);

    std::pair<double, double> get_error_dilation() const;

    // Compute the volume in the spatial configuration
    double compute_vol_current() const;

    // Print information to screen in a pleasing way...
    static void print_conv_header();

    void print_conv_footer();

    // Store the outputs in a separate folder
    char save_dir[80];
  };

  // @sect3{Implementation of the <code>Solid</code> class}

  // @sect4{Public interface}

  // We initialize the Solid class using data extracted from the parameter file.
  template <int dim>
  Solid<dim>::Solid(const std::string &input_file)
    : parameters(input_file)
    , vol_reference(0.)
    , triangulation(Triangulation<dim>::maximum_smoothing)
    , time(parameters.end_time, parameters.delta_t)
    , timer(std::cout, TimerOutput::summary, TimerOutput::wall_times)
    , u_dir(parameters.pull_time_start,
            parameters.pull_time_end,
            parameters.pull_strain,
            parameters.pull_strain_rate,
            parameters.length * parameters.scale)
    , activation_function(parameters.activation_start,
                          parameters.activation_end,
                          parameters.activation_level)
    , degree(parameters.poly_degree)
    ,
    // The Finite Element System is composed of dim continuous displacement
    // DOFs, and discontinuous pressure and dilatation DOFs. In an attempt to
    // satisfy the Babuska-Brezzi or LBB stability conditions (see Hughes
    // (2000)), we setup a $Q_n \times DGPM_{n-1} \times DGPM_{n-1}$
    // system. $Q_2 \times DGPM_1 \times DGPM_1$ elements satisfy this
    // condition, while $Q_1 \times DGPM_0 \times DGPM_0$ elements do
    // not. However, it has been shown that the latter demonstrate good
    // convergence characteristics nonetheless.
    fe(FE_Q<dim>(parameters.poly_degree),
       dim, // displacement
       FE_DGPMonomial<dim>(parameters.poly_degree - 1),
       1, // pressure
       FE_DGPMonomial<dim>(parameters.poly_degree - 1),
       1)
    , // dilatation
    dof_handler(triangulation)
    , dofs_per_cell(fe.n_dofs_per_cell())
    , u_fe(first_u_component)
    , p_fe(p_component)
    , J_fe(J_component)
    , dofs_per_block(n_blocks)
    , qf_cell(parameters.quad_order)
    , qf_face(parameters.quad_order)
    , n_q_points(qf_cell.size())
    , n_q_points_f(qf_face.size())
  {
    Assert(dim == 2 || dim == 3,
           ExcMessage("This problem only works in 2 or 3 space dimensions."));
    determine_component_extractors();

    // Initialize save_dir
    std::chrono::system_clock::time_point time_now;
    time_t time_conv;
    struct tm* timeinfo;

    time_now = std::chrono::system_clock::now();
    time_conv = std::chrono::system_clock::to_time_t(time_now);
    timeinfo = localtime(&time_conv);
    strftime(save_dir,80,"%Y%m%d_%H%M%S",timeinfo);

    // Append _Q or _D depending on the type of simulation
    if (parameters.type_of_simulation == "quasi-static")
      strcat(save_dir, "_Q");
    else if (parameters.type_of_simulation == "dynamic")
      strcat(save_dir, "_D");

    // Append nonlinear solver info
    if (parameters.type_nonlinear_solver == "classicNewton")
      strcat(save_dir, "C");
    else if (parameters.type_nonlinear_solver == "acceleratedNewton")
      strcat(save_dir, "A");

    // Append linear solver info
    if (parameters.type_lin == "CG")
      strcat(save_dir, "C");
    else if (parameters.type_lin == "GMRES")
      strcat(save_dir, "G");
    else if (parameters.type_lin == "Direct")
      strcat(save_dir, "D");
    
    // Append preconditioner info
    if (parameters.type_lin == "Direct")
      strcat(save_dir, "X");
    else if (parameters.preconditioner_type == "ssor" && parameters.type_lin != "Direct")
      strcat(save_dir, "S");
    else if (parameters.preconditioner_type == "jacobi" && parameters.type_lin != "Direct")
      strcat(save_dir, "J");

    // Static condensation?
    if (parameters.use_static_condensation)
      strcat(save_dir, "T");
    else
      strcat(save_dir, "F");
  }


  // In solving the quasi-static problem, the time becomes a loading parameter,
  // i.e. we increasing the loading linearly with time, making the two concepts
  // interchangeable. We choose to increment time linearly using a constant time
  // step size.
  //
  // We start the function with preprocessing, setting the initial dilatation
  // values, and then output the initial grid before starting the simulation
  //  proper with the first time (and loading)
  // increment.
  //
  // Care must be taken (or at least some thought given) when imposing the
  // constraint $\widetilde{J}=1$ on the initial solution field. The constraint
  // corresponds to the determinant of the deformation gradient in the
  // undeformed configuration, which is the identity tensor. We use
  // FE_DGPMonomial bases to interpolate the dilatation field, thus we can't
  // simply set the corresponding dof to unity as they correspond to the
  // monomial coefficients. Thus we use the VectorTools::project function to do
  // the work for us. The VectorTools::project function requires an argument
  // indicating the hanging node constraints. We have none in this program
  // So we have to create a constraint object. In its original state, constraint
  // objects are unsorted, and have to be sorted (using the
  // AffineConstraints::close function) before they can be used. Have a look at
  // step-21 for more information. We only need to enforce the initial condition
  // on the dilatation. In order to do this, we make use of a
  // ComponentSelectFunction which acts as a mask and sets the J_component of
  // n_components to 1. This is exactly what we want. Have a look at its usage
  // in step-20 for more information.
  template <int dim>
  void Solid<dim>::run()
  {
    std::cout << "--------------------------------------------------------" << "\n"
              << "                                                        " << "\n"
              << "             F L E X O D E A L  ( L I T E )             " << "\n"
              << "                                                        " << "\n"
              << "--------------------------------------------------------" << "\n" << std::endl;

    // Create directory to store all the outputs
    if (mkdir(save_dir, 0777) == -1)
      std::cerr << "Error :  " << strerror(errno) << std::endl;
    
    // Store parameters file
    {
      std::ostringstream prm_output_filename;
      prm_output_filename << save_dir << "/parameters.prm";
      std::ofstream out(prm_output_filename.str().c_str());
      parameters.prm.print_parameters(out, 
                                      ParameterHandler::PRM | 
                                      ParameterHandler::KeepDeclarationOrder);
    }

    make_grid();
    determine_boundary_ids();
    system_setup();
    {
      AffineConstraints<double> constraints;
      constraints.close();

      const ComponentSelectFunction<dim> J_mask(J_component, n_components);

      VectorTools::project(
        dof_handler, constraints, QGauss<dim>(degree + 2), J_mask, solution_n);
    }
    output_results();
    output_along_fibre_stretch();
    time.increment();

    // We then declare the incremental solution update $\varDelta
    // \mathbf{\Xi} \dealcoloneq \{\varDelta \mathbf{u},\varDelta \widetilde{p},
    // \varDelta \widetilde{J} \}$ and start the loop over the time domain.
    //
    // At the beginning, we reset the solution update for this time step...
    BlockVector<double> solution_delta(dofs_per_block);
    while (time.current() < time.end())
      {
        solution_delta = 0.0;

        // ...solve the current time step and update total solution vector
        // $\mathbf{\Xi}_{\textrm{n}} = \mathbf{\Xi}_{\textrm{n-1}} +
        // \varDelta \mathbf{\Xi}$...
        solve_nonlinear_timestep(solution_delta);
        solution_n += solution_delta;

        // ...and plot the results before moving on happily to the next time
        // step:
        output_results();
        output_along_fibre_stretch();

        // If our computation is dynamic (rather than quasi-static),
        // then we have to update the "previous" variables. These two
        // lines below are one of the major differences with respect to 
        // the original step-44.
        if (parameters.type_of_simulation == "dynamic")
            update_timestep();

        time.increment();
      }
  }


  // @sect3{Private interface}

  // @sect4{Threading-building-blocks structures}

  // The first group of private member functions is related to parallelization.
  // We use the Threading Building Blocks library (TBB) to perform as many
  // computationally intensive distributed tasks as possible. In particular, we
  // assemble the tangent matrix and right hand side vector, the static
  // condensation contributions, and update data stored at the quadrature points
  // using TBB. Our main tool for this is the WorkStream class (see the @ref
  // threads module for more information).

  // Firstly we deal with the tangent matrix and right-hand side assembly
  // structures. The PerTaskData object stores local contributions to the global
  // system.
  template <int dim>
  struct Solid<dim>::PerTaskData_ASM
  {
    FullMatrix<double>                   cell_matrix;
    Vector<double>                       cell_rhs;
    std::vector<types::global_dof_index> local_dof_indices;

    PerTaskData_ASM(const unsigned int dofs_per_cell)
      : cell_matrix(dofs_per_cell, dofs_per_cell)
      , cell_rhs(dofs_per_cell)
      , local_dof_indices(dofs_per_cell)
    {}

    void reset()
    {
      cell_matrix = 0.0;
      cell_rhs    = 0.0;
    }
  };


  // On the other hand, the ScratchData object stores the larger objects such as
  // the shape-function values array (<code>Nx</code>) and a shape function
  // gradient and symmetric gradient vector which we will use during the
  // assembly.
  template <int dim>
  struct Solid<dim>::ScratchData_ASM
  {
    FEValues<dim>     fe_values;
    FEFaceValues<dim> fe_face_values;

    std::vector<std::vector<double>>                  Nx;
    std::vector<std::vector<Tensor<1, dim>>>          vector_Nx;
    std::vector<std::vector<Tensor<2, dim>>>          grad_Nx;
    std::vector<std::vector<SymmetricTensor<2, dim>>> symm_grad_Nx;

    ScratchData_ASM(const FiniteElement<dim> &fe_cell,
                    const QGauss<dim> &       qf_cell,
                    const UpdateFlags         uf_cell,
                    const QGauss<dim - 1> &   qf_face,
                    const UpdateFlags         uf_face)
      : fe_values(fe_cell, qf_cell, uf_cell)
      , fe_face_values(fe_cell, qf_face, uf_face)
      , Nx(qf_cell.size(), std::vector<double>(fe_cell.n_dofs_per_cell()))
      , vector_Nx(qf_cell.size(), 
                std::vector<Tensor<1,dim>>(fe_cell.n_dofs_per_cell()))
      , grad_Nx(qf_cell.size(),
                std::vector<Tensor<2, dim>>(fe_cell.n_dofs_per_cell()))
      , symm_grad_Nx(qf_cell.size(),
                     std::vector<SymmetricTensor<2, dim>>(
                       fe_cell.n_dofs_per_cell()))
    {}

    ScratchData_ASM(const ScratchData_ASM &rhs)
      : fe_values(rhs.fe_values.get_fe(),
                  rhs.fe_values.get_quadrature(),
                  rhs.fe_values.get_update_flags())
      , fe_face_values(rhs.fe_face_values.get_fe(),
                       rhs.fe_face_values.get_quadrature(),
                       rhs.fe_face_values.get_update_flags())
      , Nx(rhs.Nx)
      , vector_Nx(rhs.vector_Nx)
      , grad_Nx(rhs.grad_Nx)
      , symm_grad_Nx(rhs.symm_grad_Nx)
    {}

    void reset()
    {
      const unsigned int n_q_points      = Nx.size();
      const unsigned int n_dofs_per_cell = Nx[0].size();
      for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
        {
          Assert(Nx[q_point].size() == n_dofs_per_cell, ExcInternalError());
          Assert(vector_Nx[q_point].size() == n_dofs_per_cell, ExcInternalError());
          Assert(grad_Nx[q_point].size() == n_dofs_per_cell,
                 ExcInternalError());
          Assert(symm_grad_Nx[q_point].size() == n_dofs_per_cell,
                 ExcInternalError());
          for (unsigned int k = 0; k < n_dofs_per_cell; ++k)
            {
              Nx[q_point][k]           = 0.0;
              vector_Nx[q_point][k]    = 0.0;
              grad_Nx[q_point][k]      = 0.0;
              symm_grad_Nx[q_point][k] = 0.0;
            }
        }
    }
  };


  // Then we define structures to assemble the statically condensed tangent
  // matrix. Recall that we wish to solve for a displacement-based formulation.
  // We do the condensation at the element level as the $\widetilde{p}$ and
  // $\widetilde{J}$ fields are element-wise discontinuous.  As these operations
  // are matrix-based, we need to setup a number of matrices to store the local
  // contributions from a number of the tangent matrix sub-blocks.  We place
  // these in the PerTaskData struct.
  //
  // We choose not to reset any data in the <code>reset()</code> function as the
  // matrix extraction and replacement tools will take care of this
  template <int dim>
  struct Solid<dim>::PerTaskData_SC
  {
    FullMatrix<double>                   cell_matrix;
    std::vector<types::global_dof_index> local_dof_indices;

    FullMatrix<double> k_orig;
    FullMatrix<double> k_pu;
    FullMatrix<double> k_pJ;
    FullMatrix<double> k_JJ;
    FullMatrix<double> k_pJ_inv;
    FullMatrix<double> k_bbar;
    FullMatrix<double> A;
    FullMatrix<double> B;
    FullMatrix<double> C;

    PerTaskData_SC(const unsigned int dofs_per_cell,
                   const unsigned int n_u,
                   const unsigned int n_p,
                   const unsigned int n_J)
      : cell_matrix(dofs_per_cell, dofs_per_cell)
      , local_dof_indices(dofs_per_cell)
      , k_orig(dofs_per_cell, dofs_per_cell)
      , k_pu(n_p, n_u)
      , k_pJ(n_p, n_J)
      , k_JJ(n_J, n_J)
      , k_pJ_inv(n_p, n_J)
      , k_bbar(n_u, n_u)
      , A(n_J, n_u)
      , B(n_J, n_u)
      , C(n_p, n_u)
    {}

    void reset()
    {}
  };


  // The ScratchData object for the operations we wish to perform here is empty
  // since we need no temporary data, but it still needs to be defined for the
  // current implementation of TBB in deal.II.  So we create a dummy struct for
  // this purpose.
  template <int dim>
  struct Solid<dim>::ScratchData_SC
  {
    void reset()
    {}
  };


  // And finally we define the structures to assist with updating the quadrature
  // point information. Similar to the SC assembly process, we do not need the
  // PerTaskData object (since there is nothing to store here) but must define
  // one nonetheless. Note that this is because for the operation that we have
  // here -- updating the data on quadrature points -- the operation is purely
  // local: the things we do on every cell get consumed on every cell, without
  // any global aggregation operation as is usually the case when using the
  // WorkStream class. The fact that we still have to define a per-task data
  // structure points to the fact that the WorkStream class may be ill-suited to
  // this operation (we could, in principle simply create a new task using
  // Threads::new_task for each cell) but there is not much harm done to doing
  // it this way anyway.
  // Furthermore, should there be different material models associated with a
  // quadrature point, requiring varying levels of computational expense, then
  // the method used here could be advantageous.
  template <int dim>
  struct Solid<dim>::PerTaskData_UQPH
  {
    void reset()
    {}
  };


  // The ScratchData object will be used to store an alias for the solution
  // vector so that we don't have to copy this large data structure. We then
  // define a number of vectors to extract the solution values and gradients at
  // the quadrature points.
  template <int dim>
  struct Solid<dim>::ScratchData_UQPH
  {
    const BlockVector<double> &solution_total;

    std::vector<Tensor<1, dim>> solution_values_u_total;
    std::vector<Tensor<2, dim>> solution_grads_u_total;
    std::vector<double>         solution_values_p_total;
    std::vector<double>         solution_values_J_total;

    FEValues<dim> fe_values;

    ScratchData_UQPH(const FiniteElement<dim> & fe_cell,
                     const QGauss<dim> &        qf_cell,
                     const UpdateFlags          uf_cell,
                     const BlockVector<double> &solution_total)
      : solution_total(solution_total)
      , solution_values_u_total(qf_cell.size())
      , solution_grads_u_total(qf_cell.size())
      , solution_values_p_total(qf_cell.size())
      , solution_values_J_total(qf_cell.size())
      , fe_values(fe_cell, qf_cell, uf_cell)
    {}

    ScratchData_UQPH(const ScratchData_UQPH &rhs)
      : solution_total(rhs.solution_total)
      , solution_values_u_total(rhs.solution_values_u_total)
      , solution_grads_u_total(rhs.solution_grads_u_total)
      , solution_values_p_total(rhs.solution_values_p_total)
      , solution_values_J_total(rhs.solution_values_J_total)
      , fe_values(rhs.fe_values.get_fe(),
                  rhs.fe_values.get_quadrature(),
                  rhs.fe_values.get_update_flags())
    {}

    void reset()
    {
      const unsigned int n_q_points = solution_grads_u_total.size();
      for (unsigned int q = 0; q < n_q_points; ++q)
        {
          solution_values_u_total[q] = 0.0;
          solution_grads_u_total[q]  = 0.0;
          solution_values_p_total[q] = 0.0;
          solution_values_J_total[q] = 0.0;
        }
    }
  };


  // @sect4{Solid::make_grid}

  // On to the first of the private member functions. Here we create the
  // triangulation of the domain, for which we choose the scaled cube with each
  // face given a boundary ID number.  The grid must be refined at least once
  // for the indentation problem.
  //
  // We then determine the volume of the reference configuration and print it
  // for comparison:
  template <int dim>
  void Solid<dim>::make_grid()
  {
    GridGenerator::hyper_rectangle(
      triangulation,
      (dim == 3 ? Point<dim>(0.0, 0.0, 0.0) : Point<dim>(0.0, 0.0)),
      (dim == 3 ? Point<dim>(parameters.length, parameters.height, parameters.width) : Point<dim>(parameters.length, parameters.height)),
      true);
    GridTools::scale(parameters.scale, triangulation);
    triangulation.refine_global(std::max(1U, parameters.global_refinement));

    vol_reference = GridTools::volume(triangulation);
    std::cout << "Grid:\n\t Reference volume: " << vol_reference << std::endl;

    /*
      BLOCK OF CODE REMOVED. MUSCLE BLOCK WON'T HAVE ID = 6 FOR NOW.
    */

    // Output grid
    {
      std::ostringstream filename;
      filename << save_dir << "/grid-" << dim << "d"<< ".msh";
      GridOut           grid_out;
      GridOutFlags::Msh write_flags;
      write_flags.write_faces = true;
      grid_out.set_flags(write_flags);
      std::ofstream output(filename.str().c_str());
      grid_out.write_msh(triangulation, output);
    }
  }

  // @sect4{Solid::determine_boundary_ids}

  // This is a simple function to retrieve all the boundary IDs set in the mesh.
  // While this might be simple for a block geometry (the IDs are {0,1,2,3,4,5}),
  // it might not be the case for other types of meshes. This will allow us to
  // output forces for each boundary ID.
  template <int dim>
  void Solid<dim>::determine_boundary_ids()
  {
    // Loop over all boundary cells and boundary faces
    for (const auto &cell : triangulation.active_cell_iterators())
      if (cell->at_boundary())
        for (unsigned int face = 0; face < GeometryInfo<dim>::faces_per_cell; ++face)
          if (cell->face(face)->at_boundary())
          {
            const unsigned int id = cell->face(face)->boundary_id();
            const bool id_exists_in_list = std::find(std::begin(list_of_boundary_ids), 
                                                     std::end(list_of_boundary_ids), id) 
                                                     != std::end(list_of_boundary_ids);
            if (!id_exists_in_list)
              list_of_boundary_ids.push_back(id);
          }

    // Sort the array
    std::sort(list_of_boundary_ids.begin(), list_of_boundary_ids.end());
  }


  // @sect4{Solid::system_setup}

  // Next we describe how the FE system is setup.  We first determine the number
  // of components per block. Since the displacement is a vector component, the
  // first dim components belong to it, while the next two describe scalar
  // pressure and dilatation DOFs.
  template <int dim>
  void Solid<dim>::system_setup()
  {
    timer.enter_subsection("Setup system");

    std::vector<unsigned int> block_component(n_components,
                                              u_dof); // Displacement
    block_component[p_component] = p_dof;             // Pressure
    block_component[J_component] = J_dof;             // Dilatation

    // The DOF handler is then initialized and we renumber the grid in an
    // efficient manner. We also record the number of DOFs per block.
    dof_handler.distribute_dofs(fe);
    DoFRenumbering::Cuthill_McKee(dof_handler);
    DoFRenumbering::component_wise(dof_handler, block_component);

    dofs_per_block =
      DoFTools::count_dofs_per_fe_block(dof_handler, block_component);

    std::cout << "Triangulation:"
              << "\n\t Number of active cells: "
              << triangulation.n_active_cells()
              << "\n\t Number of degrees of freedom: " << dof_handler.n_dofs()
              << std::endl;

    // Setup the sparsity pattern and tangent matrix
    tangent_matrix.clear();
    {
      const types::global_dof_index n_dofs_u = dofs_per_block[u_dof];
      const types::global_dof_index n_dofs_p = dofs_per_block[p_dof];
      const types::global_dof_index n_dofs_J = dofs_per_block[J_dof];

      BlockDynamicSparsityPattern dsp(n_blocks, n_blocks);

      dsp.block(u_dof, u_dof).reinit(n_dofs_u, n_dofs_u);
      dsp.block(u_dof, p_dof).reinit(n_dofs_u, n_dofs_p);
      dsp.block(u_dof, J_dof).reinit(n_dofs_u, n_dofs_J);

      dsp.block(p_dof, u_dof).reinit(n_dofs_p, n_dofs_u);
      dsp.block(p_dof, p_dof).reinit(n_dofs_p, n_dofs_p);
      dsp.block(p_dof, J_dof).reinit(n_dofs_p, n_dofs_J);

      dsp.block(J_dof, u_dof).reinit(n_dofs_J, n_dofs_u);
      dsp.block(J_dof, p_dof).reinit(n_dofs_J, n_dofs_p);
      dsp.block(J_dof, J_dof).reinit(n_dofs_J, n_dofs_J);
      dsp.collect_sizes();

      // The global system matrix initially has the following structure
      // @f{align*}
      // \underbrace{\begin{bmatrix}
      //   \mathsf{\mathbf{K}}_{uu}  & \mathsf{\mathbf{K}}_{u\widetilde{p}} &
      //   \mathbf{0}
      //   \\ \mathsf{\mathbf{K}}_{\widetilde{p}u} & \mathbf{0} &
      //   \mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}
      //   \\ \mathbf{0} & \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}} &
      //   \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
      // \end{bmatrix}}_{\mathsf{\mathbf{K}}(\mathbf{\Xi}_{\textrm{i}})}
      //      \underbrace{\begin{bmatrix}
      //          d \mathsf{u}
      //      \\  d \widetilde{\mathsf{\mathbf{p}}}
      //      \\  d \widetilde{\mathsf{\mathbf{J}}}
      //      \end{bmatrix}}_{d \mathbf{\Xi}}
      // =
      // \underbrace{\begin{bmatrix}
      //  \mathsf{\mathbf{F}}_{u}(\mathbf{u}_{\textrm{i}})
      //  \\ \mathsf{\mathbf{F}}_{\widetilde{p}}(\widetilde{p}_{\textrm{i}})
      //  \\ \mathsf{\mathbf{F}}_{\widetilde{J}}(\widetilde{J}_{\textrm{i}})
      //\end{bmatrix}}_{ \mathsf{\mathbf{F}}(\mathbf{\Xi}_{\textrm{i}}) } \, .
      // @f}
      // We optimize the sparsity pattern to reflect this structure
      // and prevent unnecessary data creation for the right-diagonal
      // block components.
      Table<2, DoFTools::Coupling> coupling(n_components, n_components);
      for (unsigned int ii = 0; ii < n_components; ++ii)
        for (unsigned int jj = 0; jj < n_components; ++jj)
          if (((ii < p_component) && (jj == J_component)) ||
              ((ii == J_component) && (jj < p_component)) ||
              ((ii == p_component) && (jj == p_component)))
            coupling[ii][jj] = DoFTools::none;
          else
            coupling[ii][jj] = DoFTools::always;
      DoFTools::make_sparsity_pattern(
        dof_handler, coupling, dsp, constraints, false);
      sparsity_pattern.copy_from(dsp);
    }

    tangent_matrix.reinit(sparsity_pattern);

    // We then set up storage vectors
    system_rhs.reinit(dofs_per_block);
    system_rhs.collect_sizes();

    solution_n.reinit(dofs_per_block);
    solution_n.collect_sizes();

    // ...and finally set up the quadrature
    // point history:
    setup_qph();

    timer.leave_subsection();
  }


  // @sect4{Solid::determine_component_extractors}
  // Next we compute some information from the FE system that describes which
  // local element DOFs are attached to which block component.  This is used
  // later to extract sub-blocks from the global matrix.
  //
  // In essence, all we need is for the FESystem object to indicate to which
  // block component a DOF on the reference cell is attached to.  Currently, the
  // interpolation fields are setup such that 0 indicates a displacement DOF, 1
  // a pressure DOF and 2 a dilatation DOF.
  template <int dim>
  void Solid<dim>::determine_component_extractors()
  {
    element_indices_u.clear();
    element_indices_p.clear();
    element_indices_J.clear();

    for (unsigned int k = 0; k < fe.n_dofs_per_cell(); ++k)
      {
        const unsigned int k_group = fe.system_to_base_index(k).first.first;
        if (k_group == u_dof)
          element_indices_u.push_back(k);
        else if (k_group == p_dof)
          element_indices_p.push_back(k);
        else if (k_group == J_dof)
          element_indices_J.push_back(k);
        else
          {
            Assert(k_group <= J_dof, ExcInternalError());
          }
      }
  }

  // @sect4{Solid::setup_qph}
  // The method used to store quadrature information is already described in
  // step-18. Here we implement a similar setup for a SMP machine.
  //
  // Firstly the actual QPH data objects are created. This must be done only
  // once the grid is refined to its finest level.
  template <int dim>
  void Solid<dim>::setup_qph()
  {
    std::cout << "    Setting up quadrature point data..." << std::endl;

    quadrature_point_history.initialize(triangulation.begin_active(),
                                        triangulation.end(),
                                        n_q_points);

    // Next we setup the initial quadrature point data.
    // Note that when the quadrature point data is retrieved,
    // it is returned as a vector of smart pointers.
    for (const auto &cell : triangulation.active_cell_iterators())
      {
        const std::vector<std::shared_ptr<PointHistory<dim>>> lqph =
          quadrature_point_history.get_data(cell);
        Assert(lqph.size() == n_q_points, ExcInternalError());

        for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
          lqph[q_point]->setup_lqp(parameters);
      }
  }

  // @sect4{Solid::update_qph_incremental}
  // As the update of QP information occurs frequently and involves a number of
  // expensive operations, we define a multithreaded approach to distributing
  // the task across a number of CPU cores.
  //
  // To start this, we first we need to obtain the total solution as it stands
  // at this Newton increment and then create the initial copy of the scratch
  // and copy data objects:
  template <int dim>
  void
  Solid<dim>::update_qph_incremental(const BlockVector<double> &solution_delta)
  {
    timer.enter_subsection("Update QPH data");
    std::cout << " UQPH " << std::flush;

    const BlockVector<double> solution_total(
      get_total_solution(solution_delta));

    const UpdateFlags uf_UQPH(update_values | update_gradients);
    PerTaskData_UQPH  per_task_data_UQPH;
    ScratchData_UQPH  scratch_data_UQPH(fe, qf_cell, uf_UQPH, solution_total);

    // We then pass them and the one-cell update function to the WorkStream to
    // be processed:
    WorkStream::run(dof_handler.active_cell_iterators(),
                    *this,
                    &Solid::update_qph_incremental_one_cell,
                    &Solid::copy_local_to_global_UQPH,
                    scratch_data_UQPH,
                    per_task_data_UQPH);

    timer.leave_subsection();
  }


  // Now we describe how we extract data from the solution vector and pass it
  // along to each QP storage object for processing.
  template <int dim>
  void Solid<dim>::update_qph_incremental_one_cell(
    const typename DoFHandler<dim>::active_cell_iterator &cell,
    ScratchData_UQPH &                                    scratch,
    PerTaskData_UQPH & /*data*/)
  {
    const std::vector<std::shared_ptr<PointHistory<dim>>> lqph =
      quadrature_point_history.get_data(cell);
    Assert(lqph.size() == n_q_points, ExcInternalError());

    Assert(scratch.solution_values_u_total.size() == n_q_points,
           ExcInternalError());
    Assert(scratch.solution_grads_u_total.size() == n_q_points,
           ExcInternalError());
    Assert(scratch.solution_values_p_total.size() == n_q_points,
           ExcInternalError());
    Assert(scratch.solution_values_J_total.size() == n_q_points,
           ExcInternalError());

    scratch.reset();

    // We first need to find the values and gradients at quadrature points
    // inside the current cell and then we update each local QP using the
    // displacement gradient and total pressure and dilatation solution
    // values:
    scratch.fe_values.reinit(cell);
    scratch.fe_values[u_fe].get_function_values(
      scratch.solution_total, scratch.solution_values_u_total);
    scratch.fe_values[u_fe].get_function_gradients(
      scratch.solution_total, scratch.solution_grads_u_total);
    scratch.fe_values[p_fe].get_function_values(
      scratch.solution_total, scratch.solution_values_p_total);
    scratch.fe_values[J_fe].get_function_values(
      scratch.solution_total, scratch.solution_values_J_total);

    for (const unsigned int q_point :
         scratch.fe_values.quadrature_point_indices())
      lqph[q_point]->update_values(scratch.solution_values_u_total[q_point],
                                   scratch.solution_grads_u_total[q_point],
                                   scratch.solution_values_p_total[q_point],
                                   scratch.solution_values_J_total[q_point],
                                   activation_function.value(time.current()),
                                   time.get_delta_t());
  }

  // This is a dummy structure that is required for Workstream.
  template <int dim>
  struct Solid<dim>::PerTaskData_TIMESTEP
  {
    void reset()
    {}
  };

  template <int dim>
  struct Solid<dim>::ScratchData_TIMESTEP
  {
    void reset()
    {}
  };

  template <int dim>
  void Solid<dim>::update_timestep()
  {
    timer.enter_subsection("Update time stepping");

    std::cout << "\n" << "-------- Updating time stepping --------" << "\n" << std::flush;

    ScratchData_TIMESTEP scratch_data_TIMESTEP;
    PerTaskData_TIMESTEP per_task_data_TIMESTEP;

    WorkStream::run(dof_handler.active_cell_iterators(),
                    *this,
                    &Solid::update_timestep_one_cell,
                    &Solid::copy_local_to_global_timestep,
                    scratch_data_TIMESTEP,
                    per_task_data_TIMESTEP);

    timer.leave_subsection();
  }

  template <int dim>
  void Solid<dim>::update_timestep_one_cell(
      const typename DoFHandler<dim>::active_cell_iterator& cell,
      ScratchData_TIMESTEP&                                 scratch,
      PerTaskData_TIMESTEP&                                 /*data*/)
  {
    const std::vector<std::shared_ptr<PointHistory<dim>>>
        lqph = quadrature_point_history.get_data(cell);
    
    Assert(lqph.size() == n_q_points, ExcInternalError());

    scratch.reset();

    for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
      lqph[q_point]->update_values_timestep(time);
  }


  // @sect4{Solid::solve_nonlinear_timestep}

  // The next function is the driver method for the Newton-Raphson scheme. At
  // its top we create a new vector to store the current Newton update step,
  // reset the error storage objects and print solver header.
  template <int dim>
  void Solid<dim>::solve_nonlinear_timestep(BlockVector<double> &solution_delta)
  {
    std::cout << std::endl
              << "Timestep " << time.get_timestep() << " @ " << time.current()
              << "s" << std::endl;

    BlockVector<double> newton_update(dofs_per_block);

    // Additional variables required for accelerated Newton method
    BlockVector<double> newton_update_previous(dofs_per_block);
    BlockVector<double> solution_k_previous(solution_n); 
    BlockVector<double> solution_k(dofs_per_block); 
    

    error_residual.reset();
    error_residual_0.reset();
    error_residual_norm.reset();
    error_update.reset();
    error_update_0.reset();
    error_update_norm.reset();

    print_conv_header();

    // We now perform a number of Newton iterations to iteratively solve the
    // nonlinear problem.  Since the problem is fully nonlinear and we are
    // using a full Newton method, the data stored in the tangent matrix and
    // right-hand side vector is not reusable and must be cleared at each
    // Newton step. We then initially build the linear system and
    // check for convergence (and store this value in the first iteration).
    // The unconstrained DOFs of the rhs vector hold the out-of-balance
    // forces, and collectively determine whether or not the equilibrium
    // solution has been attained.
    //
    // Although for this particular problem we could potentially construct the
    // RHS vector before assembling the system matrix, for the sake of
    // extensibility we choose not to do so. The benefit to assembling the RHS
    // vector and system matrix separately is that the latter is an expensive
    // operation and we can potentially avoid an extra assembly process by not
    // assembling the tangent matrix when convergence is attained. However, this
    // makes parallelizing the code using MPI more difficult. Furthermore, when
    // extending the problem to the transient case additional contributions to
    // the RHS may result from the time discretization and application of
    // constraints for the velocity and acceleration fields.
    unsigned int newton_iteration = 0;
    for (; newton_iteration < parameters.max_iterations_NR; ++newton_iteration)
      {
        std::cout << " " << std::setw(2) << newton_iteration << " "
                  << std::flush;

        // We construct the linear system, but hold off on solving it
        // (a step that should be significantly more expensive than assembly):
        make_constraints(newton_iteration);
        assemble_system();

        // We can now determine the normalized residual error and check for
        // solution convergence:
        get_error_residual(error_residual);
        if (newton_iteration == 0)
          error_residual_0 = error_residual;

        error_residual_norm = error_residual;
        //error_residual_norm.normalize(error_residual_0);

        if (newton_iteration > 0 && error_update_norm.u <= parameters.tol_u &&
            error_residual_norm.u <= parameters.tol_f)
          {
            std::cout << " CONVERGED! " << std::endl;
            print_conv_footer();

            break;
          }

        // If we have decided that we want to continue with the iteration, we
        // solve the linearized system:
        const std::pair<unsigned int, double> lin_solver_output =
          solve_linear_system(newton_update);

        // We can now determine the normalized Newton update error:
        get_error_update(newton_update, error_update);
        if (newton_iteration == 0)
          error_update_0 = error_update;

        error_update_norm = error_update;
        //error_update_norm.normalize(error_update_0);

        // Lastly, since we implicitly accept the solution step we can perform
        // the actual update of the solution increment for the current time
        // step, update all quadrature point information pertaining to
        // this new displacement and stress state and continue iterating:
        if (parameters.type_nonlinear_solver == "classicNewton")
        {
          solution_delta += newton_update;
        }
        else if (parameters.type_nonlinear_solver == "acceleratedNewton")
        {
          // Accelerated Newton-Anderson update with depth 1
          // D. G. Anderson. Iterative Procedures for Nonlinear Integral Equations. 
          // Journal of the Association for Computing Machinery 12 (1965), no. 4, 547–560. 
          // https://doi.org/10.1145/321296.321305
          // or (cleaner equations)
          // S. Pollock and H. Schwartz. Benchmarking results for the Newton-Anderson method.
          // Results in Applied Mathematics 8 (2020), 100095. 
          // https://doi.org/10.1016/j.rinam.2020.100095
          if (newton_iteration == 0)
          {
            // Usual Newton update for the very first iteration
            solution_delta += newton_update;
            solution_k = solution_n + solution_delta;
            newton_update_previous = newton_update;
          }
          else
          {
            // Now use the different direction
            const BlockVector<double> newton_delta = newton_update - newton_update_previous;
            const double gamma_update = (newton_update * newton_delta)
                                            / (newton_delta * newton_delta);
            solution_k = (1-gamma_update) * (solution_k + newton_update) + gamma_update * (solution_k_previous + newton_update_previous);
            solution_delta = solution_k - solution_n;
            // Update previous variables
            solution_k_previous = solution_k;
            newton_update_previous = newton_update;
          }
        }
        else
            Assert (false, ExcMessage("Non-linear solver type not implemented"));

        update_qph_incremental(solution_delta);

        std::cout << " | " << std::fixed << std::setprecision(3) << std::setw(7)
                  << std::scientific << lin_solver_output.first << "  "
                  << lin_solver_output.second << "  "
                  << error_residual_norm.norm << "  " << error_residual_norm.u
                  << "  " << error_residual_norm.p << "  "
                  << error_residual_norm.J << "  " << error_update_norm.norm
                  << "  " << error_update_norm.u << "  " << error_update_norm.p
                  << "  " << error_update_norm.J << "  " << std::endl;
      }

    // At the end, if it turns out that we have in fact done more iterations
    // than the parameter file allowed, we raise an exception that can be
    // caught in the main() function. The call <code>AssertThrow(condition,
    // exc_object)</code> is in essence equivalent to <code>if (!cond) throw
    // exc_object;</code> but the former form fills certain fields in the
    // exception object that identify the location (filename and line number)
    // where the exception was raised to make it simpler to identify where the
    // problem happened.
    AssertThrow(newton_iteration < parameters.max_iterations_NR,
                ExcMessage("No convergence in nonlinear solver!"));
  }


  // @sect4{Solid::print_conv_header and Solid::print_conv_footer}

  // This program prints out data in a nice table that is updated
  // on a per-iteration basis. The next two functions set up the table
  // header and footer:
  template <int dim>
  void Solid<dim>::print_conv_header()
  {
    static const unsigned int l_width = 150;

    for (unsigned int i = 0; i < l_width; ++i)
      std::cout << "_";
    std::cout << std::endl;

    std::cout << "               SOLVER STEP               "
              << " |  LIN_IT   LIN_RES    RES_NORM    "
              << " RES_U     RES_P      RES_J     NU_NORM     "
              << " NU_U       NU_P       NU_J " << std::endl;

    for (unsigned int i = 0; i < l_width; ++i)
      std::cout << "_";
    std::cout << std::endl;
  }



  template <int dim>
  void Solid<dim>::print_conv_footer()
  {
    static const unsigned int l_width = 150;

    for (unsigned int i = 0; i < l_width; ++i)
      std::cout << "_";
    std::cout << std::endl;

    const std::pair<double, double> error_dil = get_error_dilation();

    std::cout << "Relative errors:" << std::endl
              << "Displacement:\t" << error_update.u / error_update_0.u
              << std::endl
              << "Force: \t\t" << error_residual.u / error_residual_0.u
              << std::endl
              << "Dilatation:\t" << error_dil.first << std::endl
              << "v / V_0:\t" << error_dil.second * vol_reference << " / "
              << vol_reference << " = " << error_dil.second << std::endl;
  }


  // @sect4{Solid::get_error_dilation}

  // Calculate the volume of the domain in the spatial configuration
  template <int dim>
  double Solid<dim>::compute_vol_current() const
  {
    double vol_current = 0.0;

    FEValues<dim> fe_values(fe, qf_cell, update_JxW_values);

    for (const auto &cell : triangulation.active_cell_iterators())
      {
        fe_values.reinit(cell);

        // In contrast to that which was previously called for,
        // in this instance the quadrature point data is specifically
        // non-modifiable since we will only be accessing data.
        // We ensure that the right get_data function is called by
        // marking this update function as constant.
        const std::vector<std::shared_ptr<const PointHistory<dim>>> lqph =
          quadrature_point_history.get_data(cell);
        Assert(lqph.size() == n_q_points, ExcInternalError());

        for (const unsigned int q_point : fe_values.quadrature_point_indices())
          {
            const double det_F_qp = lqph[q_point]->get_det_F();
            const double JxW      = fe_values.JxW(q_point);

            vol_current += det_F_qp * JxW;
          }
      }
    Assert(vol_current > 0.0, ExcInternalError());
    return vol_current;
  }

  // Calculate how well the dilatation $\widetilde{J}$ agrees with $J
  // \dealcoloneq \textrm{det}\ \mathbf{F}$ from the $L^2$ error $ \bigl[
  // \int_{\Omega_0} {[ J - \widetilde{J}]}^{2}\textrm{d}V \bigr]^{1/2}$.
  // We also return the ratio of the current volume of the
  // domain to the reference volume. This is of interest for incompressible
  // media where we want to check how well the isochoric constraint has been
  // enforced.
  template <int dim>
  std::pair<double, double> Solid<dim>::get_error_dilation() const
  {
    double dil_L2_error = 0.0;

    FEValues<dim> fe_values(fe, qf_cell, update_JxW_values);

    for (const auto &cell : triangulation.active_cell_iterators())
      {
        fe_values.reinit(cell);

        const std::vector<std::shared_ptr<const PointHistory<dim>>> lqph =
          quadrature_point_history.get_data(cell);
        Assert(lqph.size() == n_q_points, ExcInternalError());

        for (const unsigned int q_point : fe_values.quadrature_point_indices())
          {
            const double det_F_qp   = lqph[q_point]->get_det_F();
            const double J_tilde_qp = lqph[q_point]->get_J_tilde();
            const double the_error_qp_squared =
              std::pow((det_F_qp - J_tilde_qp), 2);
            const double JxW = fe_values.JxW(q_point);

            dil_L2_error += the_error_qp_squared * JxW;
          }
      }

    return std::make_pair(std::sqrt(dil_L2_error),
                          compute_vol_current() / vol_reference);
  }


  // @sect4{Solid::get_error_residual}

  // Determine the true residual error for the problem.  That is, determine the
  // error in the residual for the unconstrained degrees of freedom.  Note that
  // to do so, we need to ignore constrained DOFs by setting the residual in
  // these vector components to zero.
  template <int dim>
  void Solid<dim>::get_error_residual(Errors &error_residual)
  {
    BlockVector<double> error_res(dofs_per_block);

    for (unsigned int i = 0; i < dof_handler.n_dofs(); ++i)
      if (!constraints.is_constrained(i))
        error_res(i) = system_rhs(i);

    error_residual.norm = error_res.l2_norm();
    error_residual.u    = error_res.block(u_dof).l2_norm();
    error_residual.p    = error_res.block(p_dof).l2_norm();
    error_residual.J    = error_res.block(J_dof).l2_norm();
  }


  // @sect4{Solid::get_error_update}

  // Determine the true Newton update error for the problem
  template <int dim>
  void Solid<dim>::get_error_update(const BlockVector<double> &newton_update,
                                    Errors &                   error_update)
  {
    BlockVector<double> error_ud(dofs_per_block);
    for (unsigned int i = 0; i < dof_handler.n_dofs(); ++i)
      if (!constraints.is_constrained(i))
        error_ud(i) = newton_update(i);

    error_update.norm = error_ud.l2_norm();
    error_update.u    = error_ud.block(u_dof).l2_norm();
    error_update.p    = error_ud.block(p_dof).l2_norm();
    error_update.J    = error_ud.block(J_dof).l2_norm();
  }



  // @sect4{Solid::get_total_solution}

  // This function provides the total solution, which is valid at any Newton
  // step. This is required as, to reduce computational error, the total
  // solution is only updated at the end of the timestep.
  template <int dim>
  BlockVector<double> Solid<dim>::get_total_solution(
    const BlockVector<double> &solution_delta) const
  {
    BlockVector<double> solution_total(solution_n);
    solution_total += solution_delta;
    return solution_total;
  }


  // @sect4{Solid::assemble_system}

  // Since we use TBB for assembly, we simply setup a copy of the
  // data structures required for the process and pass them, along
  // with the assembly functions to the WorkStream object for processing. Note
  // that we must ensure that the matrix and RHS vector are reset before any
  // assembly operations can occur. Furthermore, since we are describing a
  // problem with Neumann BCs, we will need the face normals and so must specify
  // this in the face update flags.
  template <int dim>
  void Solid<dim>::assemble_system()
  {
    timer.enter_subsection("Assemble system");
    std::cout << " ASM_SYS " << std::flush;

    tangent_matrix = 0.0;
    system_rhs     = 0.0;

    const UpdateFlags uf_cell(update_values | update_gradients |
                              update_JxW_values);
    const UpdateFlags uf_face(update_values | update_normal_vectors |
                              update_JxW_values);

    PerTaskData_ASM per_task_data(dofs_per_cell);
    ScratchData_ASM scratch_data(fe, qf_cell, uf_cell, qf_face, uf_face);

    // The syntax used here to pass data to the WorkStream class
    // is discussed in step-13.
    WorkStream::run(
      dof_handler.active_cell_iterators(),
      [this](const typename DoFHandler<dim>::active_cell_iterator &cell,
             ScratchData_ASM &                                     scratch,
             PerTaskData_ASM &                                     data) {
        this->assemble_system_one_cell(cell, scratch, data);
      },
      [this](const PerTaskData_ASM &data) {
        this->constraints.distribute_local_to_global(data.cell_matrix,
                                                     data.cell_rhs,
                                                     data.local_dof_indices,
                                                     tangent_matrix,
                                                     system_rhs);
      },
      scratch_data,
      per_task_data);

    timer.leave_subsection();
  }

  // Of course, we still have to define how we assemble the tangent matrix
  // contribution for a single cell.  We first need to reset and initialize some
  // of the scratch data structures and retrieve some basic information
  // regarding the DOF numbering on this cell.  We can precalculate the cell
  // shape function values and gradients. Note that the shape function gradients
  // are defined with regard to the current configuration.  That is
  // $\textrm{grad}\ \boldsymbol{\varphi} = \textrm{Grad}\ \boldsymbol{\varphi}
  // \ \mathbf{F}^{-1}$.
  template <int dim>
  void Solid<dim>::assemble_system_one_cell(
    const typename DoFHandler<dim>::active_cell_iterator &cell,
    ScratchData_ASM &                                     scratch,
    PerTaskData_ASM &                                     data) const
  {
    data.reset();
    scratch.reset();
    scratch.fe_values.reinit(cell);
    cell->get_dof_indices(data.local_dof_indices);

    const std::vector<std::shared_ptr<const PointHistory<dim>>> lqph =
      quadrature_point_history.get_data(cell);
    Assert(lqph.size() == n_q_points, ExcInternalError());

    for (const unsigned int q_point :
         scratch.fe_values.quadrature_point_indices())
      {
        const Tensor<2, dim> F_inv = lqph[q_point]->get_F_inv();
        for (const unsigned int k : scratch.fe_values.dof_indices())
          {
            const unsigned int k_group = fe.system_to_base_index(k).first.first;

            if (k_group == u_dof)
              {
                scratch.vector_Nx[q_point][k] = 
                  scratch.fe_values[u_fe].value(k, q_point);
                scratch.grad_Nx[q_point][k] =
                  scratch.fe_values[u_fe].gradient(k, q_point) * F_inv;
                scratch.symm_grad_Nx[q_point][k] =
                  symmetrize(scratch.grad_Nx[q_point][k]);
              }
            else if (k_group == p_dof)
              scratch.Nx[q_point][k] =
                scratch.fe_values[p_fe].value(k, q_point);
            else if (k_group == J_dof)
              scratch.Nx[q_point][k] =
                scratch.fe_values[J_fe].value(k, q_point);
            else
              Assert(k_group <= J_dof, ExcInternalError());
          }
      }

    // Now we build the local cell stiffness matrix and RHS vector. Since the
    // global and local system matrices are symmetric, we can exploit this
    // property by building only the lower half of the local matrix and copying
    // the values to the upper half.  So we only assemble half of the
    // $\mathsf{\mathbf{k}}_{uu}$, $\mathsf{\mathbf{k}}_{\widetilde{p}
    // \widetilde{p}} = \mathbf{0}$, $\mathsf{\mathbf{k}}_{\widetilde{J}
    // \widetilde{J}}$ blocks, while the whole
    // $\mathsf{\mathbf{k}}_{\widetilde{p} \widetilde{J}}$,
    // $\mathsf{\mathbf{k}}_{u \widetilde{J}} = \mathbf{0}$,
    // $\mathsf{\mathbf{k}}_{u \widetilde{p}}$ blocks are built.
    //
    // In doing so, we first extract some configuration dependent variables
    // from our quadrature history objects for the current quadrature point.
    for (const unsigned int q_point :
         scratch.fe_values.quadrature_point_indices())
      {
        const SymmetricTensor<2, dim> tau     = lqph[q_point]->get_tau();
        const Tensor<2, dim>          tau_ns  = lqph[q_point]->get_tau();
        const SymmetricTensor<4, dim> Jc      = lqph[q_point]->get_Jc();
        const double                  det_F   = lqph[q_point]->get_det_F();
        const double                  p_tilde = lqph[q_point]->get_p_tilde();
        const double                  J_tilde = lqph[q_point]->get_J_tilde();
        const double dPsi_vol_dJ   = lqph[q_point]->get_dPsi_vol_dJ();
        const double d2Psi_vol_dJ2 = lqph[q_point]->get_d2Psi_vol_dJ2();
        const SymmetricTensor<2, dim> &I =
          Physics::Elasticity::StandardTensors<dim>::I;

        // Especially for the dynamic computation, we also need the following quantities:
        const Tensor<1,dim> displacement_update   = lqph[q_point]->get_displacement();
        const Tensor<1,dim> displacement_previous = lqph[q_point]->get_displacement_previous();
        const Tensor<1,dim> velocity_previous     = lqph[q_point]->get_velocity_previous();

        // These two tensors store some precomputed data. Their use will
        // explained shortly.
        SymmetricTensor<2, dim> symm_grad_Nx_i_x_Jc;
        Tensor<1, dim>          grad_Nx_i_comp_i_x_tau;

        // Next we define some aliases to make the assembly process easier to
        // follow.
        const std::vector<double> &                 N = scratch.Nx[q_point];
        const std::vector<Tensor<1,dim>> &   vector_N = scratch.vector_Nx[q_point];
        const std::vector<SymmetricTensor<2, dim>> &symm_grad_Nx =
          scratch.symm_grad_Nx[q_point];
        const std::vector<Tensor<2, dim>> &grad_Nx = scratch.grad_Nx[q_point];
        const double                       JxW = scratch.fe_values.JxW(q_point);

        for (const unsigned int i : scratch.fe_values.dof_indices())
          {
            const unsigned int component_i =
              fe.system_to_component_index(i).first;
            const unsigned int i_group = fe.system_to_base_index(i).first.first;

            // We first compute the contributions
            // from the internal forces.  Note, by
            // definition of the rhs as the negative
            // of the residual, these contributions
            // are subtracted.
            if (i_group == u_dof)
              data.cell_rhs(i) -= (symm_grad_Nx[i] * tau) * JxW;
            else if (i_group == p_dof)
              data.cell_rhs(i) -= N[i] * (det_F - J_tilde) * JxW;
            else if (i_group == J_dof)
              data.cell_rhs(i) -= N[i] * (dPsi_vol_dJ - p_tilde) * JxW;
            else
              Assert(i_group <= J_dof, ExcInternalError());

            // Dynamic contribution. For now, we hard code the density of the
            // material. In reality, this should come from the parameters file.
            const double material_density = 1000;
            const double dt               = time.get_delta_t();

            if (i_group == u_dof && parameters.type_of_simulation == "dynamic")
            {
              data.cell_rhs(i) -= material_density * std::pow(dt,-2) * displacement_update * vector_N[i] * JxW;
              data.cell_rhs(i) += material_density * std::pow(dt,-1) * velocity_previous * vector_N[i] * JxW;
              data.cell_rhs(i) += material_density * std::pow(dt,-2) * displacement_previous * vector_N[i] * JxW;
            }

            // Before we go into the inner loop, we have one final chance to
            // introduce some optimizations. We've already taken into account
            // the symmetry of the system, and we can now precompute some
            // common terms that are repeatedly applied in the inner loop.
            // We won't be excessive here, but will rather focus on expensive
            // operations, namely those involving the rank-4 material stiffness
            // tensor and the rank-2 stress tensor.
            //
            // What we may observe is that both of these tensors are contracted
            // with shape function gradients indexed on the "i" DoF. This
            // implies that this particular operation remains constant as we
            // loop over the "j" DoF. For that reason, we can extract this from
            // the inner loop and save the many operations that, for each
            // quadrature point and DoF index "i" and repeated over index "j"
            // are required to double contract a rank-2 symmetric tensor with a
            // rank-4 symmetric tensor, and a rank-1 tensor with a rank-2
            // tensor.
            //
            // At the loss of some readability, this small change will reduce
            // the assembly time of the symmetrized system by about half when
            // using the simulation default parameters, and becomes more
            // significant as the h-refinement level increases.
            if (i_group == u_dof)
              {
                symm_grad_Nx_i_x_Jc    = symm_grad_Nx[i] * Jc;
                grad_Nx_i_comp_i_x_tau = grad_Nx[i][component_i] * tau_ns;
              }

            // Now we're prepared to compute the tangent matrix contributions:
            for (const unsigned int j :
                 scratch.fe_values.dof_indices_ending_at(i))
              {
                const unsigned int component_j =
                  fe.system_to_component_index(j).first;
                const unsigned int j_group =
                  fe.system_to_base_index(j).first.first;

                // This is the $\mathsf{\mathbf{k}}_{uu}$
                // contribution. It comprises a material contribution, and a
                // geometrical stress contribution which is only added along
                // the local matrix diagonals:
                if ((i_group == j_group) && (i_group == u_dof))
                  {
                    // The dynamical (inertial) contribution:
                    if (parameters.type_of_simulation == "dynamic")
                      data.cell_matrix(i, j) += material_density * std::pow(dt,-2) * vector_N[i] * vector_N[j] * JxW;

                    // The material contribution:
                    data.cell_matrix(i, j) += symm_grad_Nx_i_x_Jc *  //
                                              symm_grad_Nx[j] * JxW; //

                    // The geometrical stress contribution:
                    if (component_i == component_j)
                      data.cell_matrix(i, j) +=
                        grad_Nx_i_comp_i_x_tau * grad_Nx[j][component_j] * JxW;
                  }
                // Next is the $\mathsf{\mathbf{k}}_{ \widetilde{p} u}$
                // contribution
                else if ((i_group == p_dof) && (j_group == u_dof))
                  {
                    data.cell_matrix(i, j) += N[i] * det_F *               //
                                              (symm_grad_Nx[j] * I) * JxW; //
                  }
                // and lastly the $\mathsf{\mathbf{k}}_{ \widetilde{J}
                // \widetilde{p}}$ and $\mathsf{\mathbf{k}}_{ \widetilde{J}
                // \widetilde{J}}$ contributions:
                else if ((i_group == J_dof) && (j_group == p_dof))
                  data.cell_matrix(i, j) -= N[i] * N[j] * JxW;
                else if ((i_group == j_group) && (i_group == J_dof))
                  data.cell_matrix(i, j) += N[i] * d2Psi_vol_dJ2 * N[j] * JxW;
                else
                  Assert((i_group <= J_dof) && (j_group <= J_dof),
                         ExcInternalError());
              }
          }
      }

    // Next we assemble the Neumann contribution. We first check to see it the
    // cell face exists on a boundary on which a traction is applied and add
    // the contribution if this is the case.
    //
    // ********** TRACTION BOUNDARY CONDITION TEMPORARILY DISABLED *************
    //
    /* 
    for (const auto &face : cell->face_iterators())
      if (face->at_boundary() && face->boundary_id() == 6)
        {
          scratch.fe_face_values.reinit(cell, face);

          for (const unsigned int f_q_point :
               scratch.fe_face_values.quadrature_point_indices())
            {
              const Tensor<1, dim> &N =
                scratch.fe_face_values.normal_vector(f_q_point);

              // Using the face normal at this quadrature point we specify the
              // traction in reference configuration. For this problem, a
              // defined pressure is applied in the reference configuration.
              // The direction of the applied traction is assumed not to
              // evolve with the deformation of the domain. The traction is
              // defined using the first Piola-Kirchhoff stress is simply
              // $\mathbf{t} = \mathbf{P}\mathbf{N} = [p_0 \mathbf{I}]
              // \mathbf{N} = p_0 \mathbf{N}$ We use the time variable to
              // linearly ramp up the pressure load.
              //
              // Note that the contributions to the right hand side vector we
              // compute here only exist in the displacement components of the
              // vector.
              static const double p0 =
                -4.0 / (parameters.scale * parameters.scale);
              const double         time_ramp = (time.current() / time.end());
              const double         pressure  = p0 * parameters.p_p0 * time_ramp;
              const Tensor<1, dim> traction  = pressure * N;

              for (const unsigned int i : scratch.fe_values.dof_indices())
                {
                  const unsigned int i_group =
                    fe.system_to_base_index(i).first.first;

                  if (i_group == u_dof)
                    {
                      const unsigned int component_i =
                        fe.system_to_component_index(i).first;
                      const double Ni =
                        scratch.fe_face_values.shape_value(i, f_q_point);
                      const double JxW = scratch.fe_face_values.JxW(f_q_point);

                      data.cell_rhs(i) += (Ni * traction[component_i]) * JxW;
                    }
                }
            }
        }
      */

    // Finally, we need to copy the lower half of the local matrix into the
    // upper half:
    for (const unsigned int i : scratch.fe_values.dof_indices())
      for (const unsigned int j :
           scratch.fe_values.dof_indices_starting_at(i + 1))
        data.cell_matrix(i, j) = data.cell_matrix(j, i);
  }



  // @sect4{Solid::make_constraints}
  // The constraints for this problem are simple to describe.
  // In this particular example, the boundary values will be calculated for
  // the two first iterations of Newton's algorithm. In general, one would
  // build non-homogeneous constraints in the zeroth iteration (that is, when
  // `apply_dirichlet_bc == true` in the code block that follows) and build
  // only the corresponding homogeneous constraints in the following step. While
  // the current example has only homogeneous constraints, previous experiences
  // have shown that a common error is forgetting to add the extra condition
  // when refactoring the code to specific uses. This could lead to errors that
  // are hard to debug. In this spirit, we choose to make the code more verbose
  // in terms of what operations are performed at each Newton step.
  template <int dim>
  void Solid<dim>::make_constraints(const int it_nr)
  {
    // Since we (a) are dealing with an iterative Newton method, (b) are using
    // an incremental formulation for the displacement, and (c) apply the
    // constraints to the incremental displacement field, any non-homogeneous
    // constraints on the displacement update should only be specified at the
    // zeroth iteration. No subsequent contributions are to be made since the
    // constraints will be exactly satisfied after that iteration.
    const bool apply_dirichlet_bc = (it_nr == 0);

    // Furthermore, after the first Newton iteration within a timestep, the
    // constraints remain the same and we do not need to modify or rebuild them
    // so long as we do not clear the @p constraints object.
    if (it_nr > 1)
      {
        std::cout << " --- " << std::flush;
        return;
      }

    std::cout << " CST " << std::flush;

    if (apply_dirichlet_bc)
      {
        // At the zeroth Newton iteration we wish to apply the full set of
        // non-homogeneous and homogeneous constraints that represent the
        // boundary conditions on the displacement increment. Since in general
        // the constraints may be different at each time step, we need to clear
        // the constraints matrix and completely rebuild it. An example case
        // would be if a surface is accelerating; in such a scenario the change
        // in displacement is non-constant between each time step.
        constraints.clear();

        // The boundary conditions for the indentation problem in 3D are as
        // follows: On the -x, -y and -z faces (IDs 0,2,4) we set up a symmetry
        // condition to allow only planar movement while the +x and +z faces
        // (IDs 1,5) are traction free. In this contrived problem, part of the
        // +y face (ID 3) is set to have no motion in the x- and z-component.
        // Finally, as described earlier, the other part of the +y face has an
        // the applied pressure but is also constrained in the x- and
        // z-directions.
        //
        // In the following, we will have to tell the function interpolation
        // boundary values which components of the solution vector should be
        // constrained (i.e., whether it's the x-, y-, z-displacements or
        // combinations thereof). This is done using ComponentMask objects (see
        // @ref GlossComponentMask) which we can get from the finite element if we
        // provide it with an extractor object for the component we wish to
        // select. To this end we first set up such extractor objects and later
        // use it when generating the relevant component masks:
        const FEValuesExtractors::Scalar x_displacement(0);
        const FEValuesExtractors::Scalar y_displacement(1);
        const FEValuesExtractors::Scalar z_displacement(2);

        {
          const int boundary_id = 0;

          VectorTools::interpolate_boundary_values(
            dof_handler,
            boundary_id,
            Functions::ZeroFunction<dim>(n_components),
            constraints,
            (fe.component_mask(x_displacement) | fe.component_mask(y_displacement) | fe.component_mask(z_displacement)));
        }

        {
          const int boundary_id = parameters.pulling_face_id; // Which in most cases will be equal to 1

          VectorTools::interpolate_boundary_values(
            dof_handler,
            boundary_id,
            IncrementalDisplacement<dim>(
              u_dir.displacement(time.current()),u_dir.displacement(time.previous())),
            constraints,
            (fe.component_mask(x_displacement) | fe.component_mask(y_displacement) | fe.component_mask(z_displacement)));
        }

        // All other faces are traction-free.

      }
    else
      {
        // As all Dirichlet constraints are fulfilled exactly after the zeroth
        // Newton iteration, we want to ensure that no further modification are
        // made to those entries. This implies that we want to convert
        // all non-homogeneous Dirichlet constraints into homogeneous ones.
        //
        // In this example the procedure to do this is quite straightforward,
        // and in fact we can (and will) circumvent any unnecessary operations
        // when only homogeneous boundary conditions are applied.
        // In a more general problem one should be mindful of hanging node
        // and periodic constraints, which may also introduce some
        // inhomogeneities. It might then be advantageous to keep disparate
        // objects for the different types of constraints, and merge them
        // together once the homogeneous Dirichlet constraints have been
        // constructed.
        if (constraints.has_inhomogeneities())
          {
            // Since the affine constraints were finalized at the previous
            // Newton iteration, they may not be modified directly. So
            // we need to copy them to another temporary object and make
            // modification there. Once we're done, we'll transfer them
            // back to the main @p constraints object.
            AffineConstraints<double> homogeneous_constraints(constraints);
            for (unsigned int dof = 0; dof != dof_handler.n_dofs(); ++dof)
              if (homogeneous_constraints.is_inhomogeneously_constrained(dof))
                homogeneous_constraints.set_inhomogeneity(dof, 0.0);

            constraints.clear();
            constraints.copy_from(homogeneous_constraints);
          }
      }

    constraints.close();
  }

  // @sect4{Solid::assemble_sc}
  // Solving the entire block system is a bit problematic as there are no
  // contributions to the $\mathsf{\mathbf{K}}_{ \widetilde{J} \widetilde{J}}$
  // block, rendering it noninvertible (when using an iterative solver).
  // Since the pressure and dilatation variables DOFs are discontinuous, we can
  // condense them out to form a smaller displacement-only system which
  // we will then solve and subsequently post-process to retrieve the
  // pressure and dilatation solutions.

  // The static condensation process could be performed at a global level but we
  // need the inverse of one of the blocks. However, since the pressure and
  // dilatation variables are discontinuous, the static condensation (SC)
  // operation can also be done on a per-cell basis and we can produce the
  // inverse of the block-diagonal
  // $\mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}$ block by inverting the
  // local blocks. We can again use TBB to do this since each operation will be
  // independent of one another.
  //
  // Using the TBB via the WorkStream class, we assemble the contributions to
  // form
  //  $
  //  \mathsf{\mathbf{K}}_{\textrm{con}}
  //  = \bigl[ \mathsf{\mathbf{K}}_{uu} +
  //  \overline{\overline{\mathsf{\mathbf{K}}}}~ \bigr]
  //  $
  // from each element's contributions. These contributions are then added to
  // the global stiffness matrix. Given this description, the following two
  // functions should be clear:
  template <int dim>
  void Solid<dim>::assemble_sc()
  {
    timer.enter_subsection("Perform static condensation");
    std::cout << " ASM_SC " << std::flush;

    PerTaskData_SC per_task_data(dofs_per_cell,
                                 element_indices_u.size(),
                                 element_indices_p.size(),
                                 element_indices_J.size());
    ScratchData_SC scratch_data;

    WorkStream::run(dof_handler.active_cell_iterators(),
                    *this,
                    &Solid::assemble_sc_one_cell,
                    &Solid::copy_local_to_global_sc,
                    scratch_data,
                    per_task_data);

    timer.leave_subsection();
  }


  template <int dim>
  void Solid<dim>::copy_local_to_global_sc(const PerTaskData_SC &data)
  {
    for (unsigned int i = 0; i < dofs_per_cell; ++i)
      for (unsigned int j = 0; j < dofs_per_cell; ++j)
        tangent_matrix.add(data.local_dof_indices[i],
                           data.local_dof_indices[j],
                           data.cell_matrix(i, j));
  }


  // Now we describe the static condensation process. As per usual, we must
  // first find out which global numbers the degrees of freedom on this cell
  // have and reset some data structures:
  template <int dim>
  void Solid<dim>::assemble_sc_one_cell(
    const typename DoFHandler<dim>::active_cell_iterator &cell,
    ScratchData_SC &                                      scratch,
    PerTaskData_SC &                                      data)
  {
    data.reset();
    scratch.reset();
    cell->get_dof_indices(data.local_dof_indices);

    // We now extract the contribution of the dofs associated with the current
    // cell to the global stiffness matrix.  The discontinuous nature of the
    // $\widetilde{p}$ and $\widetilde{J}$ interpolations mean that their is
    // no coupling of the local contributions at the global level. This is not
    // the case with the $\mathbf{u}$ dof.  In other words,
    // $\mathsf{\mathbf{k}}_{\widetilde{J} \widetilde{p}}$,
    // $\mathsf{\mathbf{k}}_{\widetilde{p} \widetilde{p}}$ and
    // $\mathsf{\mathbf{k}}_{\widetilde{J} \widetilde{p}}$, when extracted
    // from the global stiffness matrix are the element contributions.  This
    // is not the case for $\mathsf{\mathbf{k}}_{uu}$.
    //
    // Note: A lower-case symbol is used to denote element stiffness matrices.

    // Currently the matrix corresponding to
    // the dof associated with the current element
    // (denoted somewhat loosely as $\mathsf{\mathbf{k}}$)
    // is of the form:
    // @f{align*}
    //    \begin{bmatrix}
    //       \mathsf{\mathbf{k}}_{uu}  &  \mathsf{\mathbf{k}}_{u\widetilde{p}}
    //       & \mathbf{0}
    //    \\ \mathsf{\mathbf{k}}_{\widetilde{p}u} & \mathbf{0}  &
    //    \mathsf{\mathbf{k}}_{\widetilde{p}\widetilde{J}}
    //    \\ \mathbf{0}  &  \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{p}}  &
    //    \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{J}} \end{bmatrix}
    // @f}
    //
    // We now need to modify it such that it appear as
    // @f{align*}
    //    \begin{bmatrix}
    //       \mathsf{\mathbf{k}}_{\textrm{con}}   &
    //       \mathsf{\mathbf{k}}_{u\widetilde{p}}    & \mathbf{0}
    //    \\ \mathsf{\mathbf{k}}_{\widetilde{p}u} & \mathbf{0} &
    //    \mathsf{\mathbf{k}}_{\widetilde{p}\widetilde{J}}^{-1}
    //    \\ \mathbf{0} & \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{p}} &
    //    \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{J}} \end{bmatrix}
    // @f}
    // with $\mathsf{\mathbf{k}}_{\textrm{con}} = \bigl[
    // \mathsf{\mathbf{k}}_{uu} +\overline{\overline{\mathsf{\mathbf{k}}}}~
    // \bigr]$ where $               \overline{\overline{\mathsf{\mathbf{k}}}}
    // \dealcoloneq \mathsf{\mathbf{k}}_{u\widetilde{p}}
    // \overline{\mathsf{\mathbf{k}}} \mathsf{\mathbf{k}}_{\widetilde{p}u}
    // $
    // and
    // $
    //    \overline{\mathsf{\mathbf{k}}} =
    //     \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{p}}^{-1}
    //     \mathsf{\mathbf{k}}_{\widetilde{J}\widetilde{J}}
    //    \mathsf{\mathbf{k}}_{\widetilde{p}\widetilde{J}}^{-1}
    // $.
    //
    // At this point, we need to take note of
    // the fact that global data already exists
    // in the $\mathsf{\mathbf{K}}_{uu}$,
    // $\mathsf{\mathbf{K}}_{\widetilde{p} \widetilde{J}}$
    // and
    //  $\mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{p}}$
    // sub-blocks.  So if we are to modify them, we must account for the data
    // that is already there (i.e. simply add to it or remove it if
    // necessary).  Since the copy_local_to_global operation is a "+="
    // operation, we need to take this into account
    //
    // For the $\mathsf{\mathbf{K}}_{uu}$ block in particular, this means that
    // contributions have been added from the surrounding cells, so we need to
    // be careful when we manipulate this block.  We can't just erase the
    // sub-blocks.
    //
    // This is the strategy we will employ to get the sub-blocks we want:
    //
    // - $ {\mathsf{\mathbf{k}}}_{\textrm{store}}$:
    // Since we don't have access to $\mathsf{\mathbf{k}}_{uu}$,
    // but we know its contribution is added to
    // the global $\mathsf{\mathbf{K}}_{uu}$ matrix, we just want
    // to add the element wise
    // static-condensation $\overline{\overline{\mathsf{\mathbf{k}}}}$.
    //
    // - $\mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}$:
    //                      Similarly, $\mathsf{\mathbf{k}}_{\widetilde{p}
    //                      \widetilde{J}}$ exists in
    //          the subblock. Since the copy
    //          operation is a += operation, we
    //          need to subtract the existing
    //          $\mathsf{\mathbf{k}}_{\widetilde{p} \widetilde{J}}$
    //                      submatrix in addition to
    //          "adding" that which we wish to
    //          replace it with.
    //
    // - $\mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{p}}$:
    //              Since the global matrix
    //          is symmetric, this block is the
    //          same as the one above and we
    //          can simply use
    //              $\mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}$
    //          as a substitute for this one.
    //
    // We first extract element data from the
    // system matrix. So first we get the
    // entire subblock for the cell, then
    // extract $\mathsf{\mathbf{k}}$
    // for the dofs associated with
    // the current element
    data.k_orig.extract_submatrix_from(tangent_matrix,
                                       data.local_dof_indices,
                                       data.local_dof_indices);
    // and next the local matrices for
    // $\mathsf{\mathbf{k}}_{ \widetilde{p} u}$
    // $\mathsf{\mathbf{k}}_{ \widetilde{p} \widetilde{J}}$
    // and
    // $\mathsf{\mathbf{k}}_{ \widetilde{J} \widetilde{J}}$:
    data.k_pu.extract_submatrix_from(data.k_orig,
                                     element_indices_p,
                                     element_indices_u);
    data.k_pJ.extract_submatrix_from(data.k_orig,
                                     element_indices_p,
                                     element_indices_J);
    data.k_JJ.extract_submatrix_from(data.k_orig,
                                     element_indices_J,
                                     element_indices_J);

    // To get the inverse of $\mathsf{\mathbf{k}}_{\widetilde{p}
    // \widetilde{J}}$, we invert it directly.  This operation is relatively
    // inexpensive since $\mathsf{\mathbf{k}}_{\widetilde{p} \widetilde{J}}$
    // since block-diagonal.
    data.k_pJ_inv.invert(data.k_pJ);

    // Now we can make condensation terms to
    // add to the $\mathsf{\mathbf{k}}_{uu}$
    // block and put them in
    // the cell local matrix
    //    $
    //    \mathsf{\mathbf{A}}
    //    =
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}
    //    \mathsf{\mathbf{k}}_{\widetilde{p} u}
    //    $:
    data.k_pJ_inv.mmult(data.A, data.k_pu);
    //      $
    //      \mathsf{\mathbf{B}}
    //      =
    //      \mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{J}}
    //      \mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}
    //      \mathsf{\mathbf{k}}_{\widetilde{p} u}
    //      $
    data.k_JJ.mmult(data.B, data.A);
    //    $
    //    \mathsf{\mathbf{C}}
    //    =
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{p}}
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{J}}
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}
    //    \mathsf{\mathbf{k}}_{\widetilde{p} u}
    //    $
    data.k_pJ_inv.Tmmult(data.C, data.B);
    //    $
    //    \overline{\overline{\mathsf{\mathbf{k}}}}
    //    =
    //    \mathsf{\mathbf{k}}_{u \widetilde{p}}
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{p}}
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{J} \widetilde{J}}
    //    \mathsf{\mathbf{k}}^{-1}_{\widetilde{p} \widetilde{J}}
    //    \mathsf{\mathbf{k}}_{\widetilde{p} u}
    //    $
    data.k_pu.Tmmult(data.k_bbar, data.C);
    data.k_bbar.scatter_matrix_to(element_indices_u,
                                  element_indices_u,
                                  data.cell_matrix);

    // Next we place
    // $\mathsf{\mathbf{k}}^{-1}_{ \widetilde{p} \widetilde{J}}$
    // in the
    // $\mathsf{\mathbf{k}}_{ \widetilde{p} \widetilde{J}}$
    // block for post-processing.  Note again
    // that we need to remove the
    // contribution that already exists there.
    data.k_pJ_inv.add(-1.0, data.k_pJ);
    data.k_pJ_inv.scatter_matrix_to(element_indices_p,
                                    element_indices_J,
                                    data.cell_matrix);
  }

  // @sect4{Solid::solve_linear_system}
  // We now have all of the necessary components to use one of two possible
  // methods to solve the linearised system. The first is to perform static
  // condensation on an element level, which requires some alterations
  // to the tangent matrix and RHS vector. Alternatively, the full block
  // system can be solved by performing condensation on a global level.
  // Below we implement both approaches.
  template <int dim>
  std::pair<unsigned int, double>
  Solid<dim>::solve_linear_system(BlockVector<double> &newton_update)
  {
    unsigned int lin_it  = 0;
    double       lin_res = 0.0;

    if (parameters.use_static_condensation == true)
      {
        // Firstly, here is the approach using the (permanent) augmentation of
        // the tangent matrix. For the following, recall that
        // @f{align*}
        //  \mathsf{\mathbf{K}}_{\textrm{store}}
        //\dealcoloneq
        //  \begin{bmatrix}
        //      \mathsf{\mathbf{K}}_{\textrm{con}}      &
        //      \mathsf{\mathbf{K}}_{u\widetilde{p}}    & \mathbf{0}
        //  \\  \mathsf{\mathbf{K}}_{\widetilde{p}u}    &       \mathbf{0} &
        //  \mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}^{-1}
        //  \\  \mathbf{0}      &
        //  \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}                &
        //  \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}} \end{bmatrix} \, .
        // @f}
        // and
        //  @f{align*}
        //              d \widetilde{\mathsf{\mathbf{p}}}
        //              & =
        //              \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
        //              \bigl[
        //                       \mathsf{\mathbf{F}}_{\widetilde{J}}
        //                       -
        //                       \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
        //                       d \widetilde{\mathsf{\mathbf{J}}} \bigr]
        //              \\ d \widetilde{\mathsf{\mathbf{J}}}
        //              & =
        //              \mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}^{-1}
        //              \bigl[
        //                      \mathsf{\mathbf{F}}_{\widetilde{p}}
        //                      - \mathsf{\mathbf{K}}_{\widetilde{p}u} d
        //                      \mathsf{\mathbf{u}} \bigr]
        //               \\ \Rightarrow d \widetilde{\mathsf{\mathbf{p}}}
        //              &= \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
        //              \mathsf{\mathbf{F}}_{\widetilde{J}}
        //              -
        //              \underbrace{\bigl[\mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
        //              \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
        //              \mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}^{-1}\bigr]}_{\overline{\mathsf{\mathbf{K}}}}\bigl[
        //              \mathsf{\mathbf{F}}_{\widetilde{p}}
        //              - \mathsf{\mathbf{K}}_{\widetilde{p}u} d
        //              \mathsf{\mathbf{u}} \bigr]
        //  @f}
        //  and thus
        //  @f[
        //              \underbrace{\bigl[ \mathsf{\mathbf{K}}_{uu} +
        //              \overline{\overline{\mathsf{\mathbf{K}}}}~ \bigr]
        //              }_{\mathsf{\mathbf{K}}_{\textrm{con}}} d
        //              \mathsf{\mathbf{u}}
        //              =
        //          \underbrace{
        //              \Bigl[
        //              \mathsf{\mathbf{F}}_{u}
        //                      - \mathsf{\mathbf{K}}_{u\widetilde{p}} \bigl[
        //                      \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
        //                      \mathsf{\mathbf{F}}_{\widetilde{J}}
        //                      -
        //                      \overline{\mathsf{\mathbf{K}}}\mathsf{\mathbf{F}}_{\widetilde{p}}
        //                      \bigr]
        //              \Bigr]}_{\mathsf{\mathbf{F}}_{\textrm{con}}}
        //  @f]
        //  where
        //  @f[
        //              \overline{\overline{\mathsf{\mathbf{K}}}} \dealcoloneq
        //                      \mathsf{\mathbf{K}}_{u\widetilde{p}}
        //                      \overline{\mathsf{\mathbf{K}}}
        //                      \mathsf{\mathbf{K}}_{\widetilde{p}u} \, .
        //  @f]

        // At the top, we allocate two temporary vectors to help with the
        // static condensation, and variables to store the number of
        // linear solver iterations and the (hopefully converged) residual.
        BlockVector<double> A(dofs_per_block);
        BlockVector<double> B(dofs_per_block);


        // In the first step of this function, we solve for the incremental
        // displacement $d\mathbf{u}$.  To this end, we perform static
        // condensation to make
        //    $\mathsf{\mathbf{K}}_{\textrm{con}}
        //    = \bigl[ \mathsf{\mathbf{K}}_{uu} +
        //    \overline{\overline{\mathsf{\mathbf{K}}}}~ \bigr]$
        // and put
        // $\mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}$
        // in the original $\mathsf{\mathbf{K}}_{\widetilde{p} \widetilde{J}}$
        // block. That is, we make $\mathsf{\mathbf{K}}_{\textrm{store}}$.
        {
          assemble_sc();

          //              $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //      =
          //              \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //              \mathsf{\mathbf{F}}_{\widetilde{p}}
          //              $
          tangent_matrix.block(p_dof, J_dof)
            .vmult(A.block(J_dof), system_rhs.block(p_dof));
          //      $
          //      \mathsf{\mathbf{B}}_{\widetilde{J}}
          //      =
          //      \mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{J}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      $
          tangent_matrix.block(J_dof, J_dof)
            .vmult(B.block(J_dof), A.block(J_dof));
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //      =
          //      \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      -
          //      \mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{J}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      $
          A.block(J_dof) = system_rhs.block(J_dof);
          A.block(J_dof) -= B.block(J_dof);
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //      =
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{J} \widetilde{p}}
          //      [
          //      \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      -
          //      \mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{J}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      ]
          //      $
          tangent_matrix.block(p_dof, J_dof)
            .Tvmult(A.block(p_dof), A.block(J_dof));
          //      $
          //      \mathsf{\mathbf{A}}_{u}
          //      =
          //      \mathsf{\mathbf{K}}_{u \widetilde{p}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{J} \widetilde{p}}
          //      [
          //      \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      -
          //      \mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{J}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      ]
          //      $
          tangent_matrix.block(u_dof, p_dof)
            .vmult(A.block(u_dof), A.block(p_dof));
          //      $
          //      \mathsf{\mathbf{F}}_{\text{con}}
          //      =
          //      \mathsf{\mathbf{F}}_{u}
          //      -
          //      \mathsf{\mathbf{K}}_{u \widetilde{p}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{J} \widetilde{p}}
          //      [
          //      \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      -
          //      \mathsf{\mathbf{K}}_{\widetilde{J} \widetilde{J}}
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p} \widetilde{J}}
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      ]
          //      $
          system_rhs.block(u_dof) -= A.block(u_dof);

          timer.enter_subsection("Linear solver");
          std::cout << " SLV " << std::flush;
          if (parameters.type_lin == "CG")
            {
              const auto solver_its = static_cast<unsigned int>(
                tangent_matrix.block(u_dof, u_dof).m() *
                parameters.max_iterations_lin);
              const double tol_sol =
                parameters.tol_lin * system_rhs.block(u_dof).l2_norm();

              SolverControl solver_control(solver_its, tol_sol);

              GrowingVectorMemory<Vector<double>> GVM;
              SolverCG<Vector<double>> solver_CG(solver_control, GVM);

              // We've chosen by default a SSOR preconditioner as it appears to
              // provide the fastest solver convergence characteristics for this
              // problem on a single-thread machine.  However, this might not be
              // true for different problem sizes.
              PreconditionSelector<SparseMatrix<double>, Vector<double>>
                preconditioner(parameters.preconditioner_type,
                               parameters.preconditioner_relaxation);
              preconditioner.use_matrix(tangent_matrix.block(u_dof, u_dof));

              solver_CG.solve(tangent_matrix.block(u_dof, u_dof),
                              newton_update.block(u_dof),
                              system_rhs.block(u_dof),
                              preconditioner);

              lin_it  = solver_control.last_step();
              lin_res = solver_control.last_value();
            }
          else if (parameters.type_lin == "GMRES")
            {
              const auto solver_its = static_cast<unsigned int>(
                tangent_matrix.block(u_dof, u_dof).m() *
                parameters.max_iterations_lin);
              const double tol_sol =
                parameters.tol_lin * system_rhs.block(u_dof).l2_norm();

              SolverControl solver_control(solver_its, tol_sol);

              GrowingVectorMemory<Vector<double> > GVM;
              SolverGMRES<Vector<double> > solver_GMRES(solver_control, GVM);

              PreconditionSelector<SparseMatrix<double>, Vector<double> >
                preconditioner(parameters.preconditioner_type,
                               parameters.preconditioner_relaxation);
              preconditioner.use_matrix(tangent_matrix.block(u_dof, u_dof));

              solver_GMRES.solve(tangent_matrix.block(u_dof, u_dof),
                                 newton_update.block(u_dof),
                                 system_rhs.block(u_dof),
                                 preconditioner);

              lin_it = solver_control.last_step();
              lin_res = solver_control.last_value();
            }
          else if (parameters.type_lin == "Direct")
            {
              // Otherwise if the problem is small
              // enough, a direct solver can be
              // utilised.
              SparseDirectUMFPACK A_direct;
              A_direct.initialize(tangent_matrix.block(u_dof, u_dof));
              A_direct.vmult(newton_update.block(u_dof),
                             system_rhs.block(u_dof));

              lin_it  = 1;
              lin_res = 0.0;
            }
          else
            Assert(false, ExcMessage("Linear solver type not implemented"));

          timer.leave_subsection();
        }

        // Now that we have the displacement update, distribute the constraints
        // back to the Newton update:
        constraints.distribute(newton_update);

        timer.enter_subsection("Linear solver postprocessing");
        std::cout << " PP " << std::flush;

        // The next step after solving the displacement
        // problem is to post-process to get the
        // dilatation solution from the
        // substitution:
        //    $
        //     d \widetilde{\mathsf{\mathbf{J}}}
        //      = \mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}^{-1} \bigl[
        //       \mathsf{\mathbf{F}}_{\widetilde{p}}
        //     - \mathsf{\mathbf{K}}_{\widetilde{p}u} d \mathsf{\mathbf{u}}
        //      \bigr]
        //    $
        {
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{p}}
          //      =
          //      \mathsf{\mathbf{K}}_{\widetilde{p}u} d \mathsf{\mathbf{u}}
          //      $
          tangent_matrix.block(p_dof, u_dof)
            .vmult(A.block(p_dof), newton_update.block(u_dof));
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{p}}
          //      =
          //      -\mathsf{\mathbf{K}}_{\widetilde{p}u} d \mathsf{\mathbf{u}}
          //      $
          A.block(p_dof) *= -1.0;
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{p}}
          //      =
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      -\mathsf{\mathbf{K}}_{\widetilde{p}u} d \mathsf{\mathbf{u}}
          //      $
          A.block(p_dof) += system_rhs.block(p_dof);
          //      $
          //      d\mathsf{\mathbf{\widetilde{J}}}
          //      =
          //      \mathsf{\mathbf{K}}^{-1}_{\widetilde{p}\widetilde{J}}
          //      [
          //      \mathsf{\mathbf{F}}_{\widetilde{p}}
          //      -\mathsf{\mathbf{K}}_{\widetilde{p}u} d \mathsf{\mathbf{u}}
          //      ]
          //      $
          tangent_matrix.block(p_dof, J_dof)
            .vmult(newton_update.block(J_dof), A.block(p_dof));
        }

        // we ensure here that any Dirichlet constraints
        // are distributed on the updated solution:
        constraints.distribute(newton_update);

        // Finally we solve for the pressure
        // update with the substitution:
        //    $
        //    d \widetilde{\mathsf{\mathbf{p}}}
        //     =
        //    \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
        //    \bigl[
        //     \mathsf{\mathbf{F}}_{\widetilde{J}}
        //      - \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
        //    d \widetilde{\mathsf{\mathbf{J}}}
        //    \bigr]
        //    $
        {
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //       =
          //      \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
          //      d \widetilde{\mathsf{\mathbf{J}}}
          //      $
          tangent_matrix.block(J_dof, J_dof)
            .vmult(A.block(J_dof), newton_update.block(J_dof));
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //       =
          //      -\mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
          //      d \widetilde{\mathsf{\mathbf{J}}}
          //      $
          A.block(J_dof) *= -1.0;
          //      $
          //      \mathsf{\mathbf{A}}_{\widetilde{J}}
          //       =
          //      \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      -
          //      \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
          //      d \widetilde{\mathsf{\mathbf{J}}}
          //      $
          A.block(J_dof) += system_rhs.block(J_dof);
          // and finally....
          //    $
          //    d \widetilde{\mathsf{\mathbf{p}}}
          //     =
          //    \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}
          //    \bigl[
          //     \mathsf{\mathbf{F}}_{\widetilde{J}}
          //      - \mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{J}}
          //    d \widetilde{\mathsf{\mathbf{J}}}
          //    \bigr]
          //    $
          tangent_matrix.block(p_dof, J_dof)
            .Tvmult(newton_update.block(p_dof), A.block(J_dof));
        }

        // We are now at the end, so we distribute all
        // constrained dofs back to the Newton
        // update:
        constraints.distribute(newton_update);

        timer.leave_subsection();
      }
    else
      {
        std::cout << " ------ " << std::flush;

        timer.enter_subsection("Linear solver");
        std::cout << " SLV " << std::flush;

        if (parameters.type_lin == "CG")
          {
            // Manual condensation of the dilatation and pressure fields on
            // a local level, and subsequent post-processing, took quite a
            // bit of effort to achieve. To recap, we had to produce the
            // inverse matrix
            // $\mathsf{\mathbf{K}}_{\widetilde{p}\widetilde{J}}^{-1}$, which
            // was permanently written into the global tangent matrix. We then
            // permanently modified $\mathsf{\mathbf{K}}_{uu}$ to produce
            // $\mathsf{\mathbf{K}}_{\textrm{con}}$. This involved the
            // extraction and manipulation of local sub-blocks of the tangent
            // matrix. After solving for the displacement, the individual
            // matrix-vector operations required to solve for dilatation and
            // pressure were carefully implemented. Contrast these many sequence
            // of steps to the much simpler and transparent implementation using
            // functionality provided by the LinearOperator class.

            // For ease of later use, we define some aliases for
            // blocks in the RHS vector
            const Vector<double> &f_u = system_rhs.block(u_dof);
            const Vector<double> &f_p = system_rhs.block(p_dof);
            const Vector<double> &f_J = system_rhs.block(J_dof);

            // ... and for blocks in the Newton update vector.
            Vector<double> &d_u = newton_update.block(u_dof);
            Vector<double> &d_p = newton_update.block(p_dof);
            Vector<double> &d_J = newton_update.block(J_dof);

            // We next define some linear operators for the tangent matrix
            // sub-blocks We will exploit the symmetry of the system, so not all
            // blocks are required.
            const auto K_uu =
              linear_operator(tangent_matrix.block(u_dof, u_dof));
            const auto K_up =
              linear_operator(tangent_matrix.block(u_dof, p_dof));
            const auto K_pu =
              linear_operator(tangent_matrix.block(p_dof, u_dof));
            const auto K_Jp =
              linear_operator(tangent_matrix.block(J_dof, p_dof));
            const auto K_JJ =
              linear_operator(tangent_matrix.block(J_dof, J_dof));

            // We then construct a LinearOperator that represents the inverse of
            // (square block)
            // $\mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}$. Since it is
            // diagonal (or, when a higher order ansatz it used, nearly
            // diagonal), a Jacobi preconditioner is suitable.
            PreconditionSelector<SparseMatrix<double>, Vector<double>>
              preconditioner_K_Jp_inv("jacobi");
            preconditioner_K_Jp_inv.use_matrix(
              tangent_matrix.block(J_dof, p_dof));
            ReductionControl solver_control_K_Jp_inv(
              static_cast<unsigned int>(tangent_matrix.block(J_dof, p_dof).m() *
                                        parameters.max_iterations_lin),
              1.0e-30,
              parameters.tol_lin);
            SolverSelector<Vector<double>> solver_K_Jp_inv;
            solver_K_Jp_inv.select("cg");
            solver_K_Jp_inv.set_control(solver_control_K_Jp_inv);
            const auto K_Jp_inv =
              inverse_operator(K_Jp, solver_K_Jp_inv, preconditioner_K_Jp_inv);

            // Now we can construct that transpose of
            // $\mathsf{\mathbf{K}}_{\widetilde{J}\widetilde{p}}^{-1}$ and a
            // linear operator that represents the condensed operations
            // $\overline{\mathsf{\mathbf{K}}}$ and
            // $\overline{\overline{\mathsf{\mathbf{K}}}}$ and the final
            // augmented matrix
            // $\mathsf{\mathbf{K}}_{\textrm{con}}$.
            // Note that the schur_complement() operator could also be of use
            // here, but for clarity and the purpose of demonstrating the
            // similarities between the formulation and implementation of the
            // linear solution scheme, we will perform these operations
            // manually.
            const auto K_pJ_inv     = transpose_operator(K_Jp_inv);
            const auto K_pp_bar     = K_Jp_inv * K_JJ * K_pJ_inv;
            const auto K_uu_bar_bar = K_up * K_pp_bar * K_pu;
            const auto K_uu_con     = K_uu + K_uu_bar_bar;

            // Lastly, we define an operator for inverse of augmented stiffness
            // matrix, namely $\mathsf{\mathbf{K}}_{\textrm{con}}^{-1}$. Note
            // that the preconditioner for the augmented stiffness matrix is
            // different to the case when we use static condensation. In this
            // instance, the preconditioner is based on a non-modified
            // $\mathsf{\mathbf{K}}_{uu}$, while with the first approach we
            // actually modified the entries of this sub-block. However, since
            // $\mathsf{\mathbf{K}}_{\textrm{con}}$ and
            // $\mathsf{\mathbf{K}}_{uu}$ operate on the same space, it remains
            // adequate for this problem.
            PreconditionSelector<SparseMatrix<double>, Vector<double>>
              preconditioner_K_con_inv(parameters.preconditioner_type,
                                       parameters.preconditioner_relaxation);
            preconditioner_K_con_inv.use_matrix(
              tangent_matrix.block(u_dof, u_dof));
            ReductionControl solver_control_K_con_inv(
              static_cast<unsigned int>(tangent_matrix.block(u_dof, u_dof).m() *
                                        parameters.max_iterations_lin),
              1.0e-30,
              parameters.tol_lin);
            SolverSelector<Vector<double>> solver_K_con_inv;
            solver_K_con_inv.select("cg");
            solver_K_con_inv.set_control(solver_control_K_con_inv);
            const auto K_uu_con_inv =
              inverse_operator(K_uu_con,
                               solver_K_con_inv,
                               preconditioner_K_con_inv);

            // Now we are in a position to solve for the displacement field.
            // We can nest the linear operations, and the result is immediately
            // written to the Newton update vector.
            // It is clear that the implementation closely mimics the derivation
            // stated in the introduction.
            d_u =
              K_uu_con_inv * (f_u - K_up * (K_Jp_inv * f_J - K_pp_bar * f_p));

            timer.leave_subsection();

            // The operations need to post-process for the dilatation and
            // pressure fields are just as easy to express.
            timer.enter_subsection("Linear solver postprocessing");
            std::cout << " PP " << std::flush;

            d_J = K_pJ_inv * (f_p - K_pu * d_u);
            d_p = K_Jp_inv * (f_J - K_JJ * d_J);

            lin_it  = solver_control_K_con_inv.last_step();
            lin_res = solver_control_K_con_inv.last_value();
          }
        else if (parameters.type_lin == "GMRES")
          {
            const Vector<double> &f_u = system_rhs.block(u_dof);
            const Vector<double> &f_p = system_rhs.block(p_dof);
            const Vector<double> &f_J = system_rhs.block(J_dof);

            Vector<double> &d_u = newton_update.block(u_dof);
            Vector<double> &d_p = newton_update.block(p_dof);
            Vector<double> &d_J = newton_update.block(J_dof);

            const auto K_uu = linear_operator(tangent_matrix.block(u_dof, u_dof));
            const auto K_up = linear_operator(tangent_matrix.block(u_dof, p_dof));
            const auto K_pu = linear_operator(tangent_matrix.block(p_dof, u_dof));
            const auto K_Jp = linear_operator(tangent_matrix.block(J_dof, p_dof));
            const auto K_JJ = linear_operator(tangent_matrix.block(J_dof, J_dof));

            PreconditionSelector< SparseMatrix<double>, Vector<double> >
            preconditioner_K_Jp_inv ("jacobi");
            preconditioner_K_Jp_inv.use_matrix(tangent_matrix.block(J_dof, p_dof));
            ReductionControl solver_control_K_Jp_inv (tangent_matrix.block(J_dof, p_dof).m() * parameters.max_iterations_lin,
                                                      1.0e-30, parameters.tol_lin);
            SolverSelector< Vector<double> > solver_K_Jp_inv;
            solver_K_Jp_inv.select("gmres");
            solver_K_Jp_inv.set_control(solver_control_K_Jp_inv);
            const auto K_Jp_inv = inverse_operator(K_Jp,
                                                   solver_K_Jp_inv,
                                                   preconditioner_K_Jp_inv);

            const auto K_pJ_inv     = transpose_operator(K_Jp_inv);
            const auto K_pp_bar     = K_Jp_inv * K_JJ * K_pJ_inv;
            const auto K_uu_bar_bar = K_up * K_pp_bar * K_pu;
            const auto K_uu_con     = K_uu + K_uu_bar_bar;

            PreconditionSelector< SparseMatrix<double>, Vector<double> >
            preconditioner_K_con_inv (parameters.preconditioner_type,
                                      parameters.preconditioner_relaxation);
            preconditioner_K_con_inv.use_matrix(tangent_matrix.block(u_dof, u_dof));
            ReductionControl solver_control_K_con_inv (tangent_matrix.block(u_dof, u_dof).m() * parameters.max_iterations_lin,
                                                       1.0e-30, parameters.tol_lin);
            SolverSelector< Vector<double> > solver_K_con_inv;
            solver_K_con_inv.select("gmres");
            solver_K_con_inv.set_control(solver_control_K_con_inv);
            const auto K_uu_con_inv = inverse_operator(K_uu_con,
                                                       solver_K_con_inv,
                                                       preconditioner_K_con_inv);

            d_u = K_uu_con_inv*(f_u - K_up*(K_Jp_inv*f_J - K_pp_bar*f_p));

            timer.leave_subsection();

            timer.enter_subsection("Linear solver postprocessing");
            std::cout << " PP " << std::flush;

            d_J = K_pJ_inv*(f_p - K_pu*d_u);
            d_p = K_Jp_inv*(f_J - K_JJ*d_J);

            lin_it = solver_control_K_con_inv.last_step();
            lin_res = solver_control_K_con_inv.last_value();
          }
        else if (parameters.type_lin == "Direct")
          {
            // Solve the full block system with
            // a direct solver. As it is relatively
            // robust, it may be immune to problem
            // arising from the presence of the zero
            // $\mathsf{\mathbf{K}}_{ \widetilde{J} \widetilde{J}}$
            // block.
            SparseDirectUMFPACK A_direct;
            A_direct.initialize(tangent_matrix);
            A_direct.vmult(newton_update, system_rhs);

            lin_it  = 1;
            lin_res = 0.0;

            std::cout << " -- " << std::flush;
          }
        else
          Assert(false, ExcMessage("Linear solver type not implemented"));

        timer.leave_subsection();

        // Finally, we again ensure here that any Dirichlet
        // constraints are distributed on the updated solution:
        constraints.distribute(newton_update);
      }

    return std::make_pair(lin_it, lin_res);
  }

  // @sect4{Solid::output_results}
  // Here we present how the results are written to file to be viewed
  // using ParaView or VisIt. The method is similar to that shown in previous
  // tutorials so will not be discussed in detail.
  template <int dim>
  void Solid<dim>::output_results() const
  {
    DataOut<dim> data_out;
    std::vector<DataComponentInterpretation::DataComponentInterpretation>
      data_component_interpretation(
        dim, DataComponentInterpretation::component_is_part_of_vector);
    data_component_interpretation.push_back(
      DataComponentInterpretation::component_is_scalar);
    data_component_interpretation.push_back(
      DataComponentInterpretation::component_is_scalar);

    std::vector<std::string> solution_name(dim, "displacement");
    solution_name.emplace_back("pressure");
    solution_name.emplace_back("dilatation");

    DataOutBase::VtkFlags output_flags;
    output_flags.write_higher_order_cells = true;
    data_out.set_flags(output_flags);

    data_out.attach_dof_handler(dof_handler);
    data_out.add_data_vector(solution_n,
                             solution_name,
                             DataOut<dim>::type_dof_data,
                             data_component_interpretation);

    // Since we are dealing with a large deformation problem, it would be nice
    // to display the result on a displaced grid!  The MappingQEulerian class
    // linked with the DataOut class provides an interface through which this
    // can be achieved without physically moving the grid points in the
    // Triangulation object ourselves.  We first need to copy the solution to
    // a temporary vector and then create the Eulerian mapping. We also
    // specify the polynomial degree to the DataOut object in order to produce
    // a more refined output data set when higher order polynomials are used.
    Vector<double> soln(solution_n.size());
    for (unsigned int i = 0; i < soln.size(); ++i)
      soln(i) = solution_n(i);
    MappingQEulerian<dim> q_mapping(degree, dof_handler, soln);
    data_out.build_patches(q_mapping, degree);

    std::ostringstream filename;
    filename << save_dir << "/solution-" << dim << "d-" 
             << std::setfill('0') << std::setw(3) << time.get_timestep() << ".vtu";
    
    std::ofstream output(filename.str().c_str());
    data_out.write_vtu(output);
  }

  // @sect4{Solid::output_along_fibre_stretch}

  // This function serves as an example of how to
  // transfer quadrature-point data into a global
  // object that can be treated in the same way
  // as the solution vector.
  template <int dim>
  void Solid<dim>::output_along_fibre_stretch() const
  {
    // Create data vectors
    FE_DGQ<dim> fe_stretch(degree);
    DoFHandler<dim> dof_handler_stretch(triangulation);
    dof_handler_stretch.distribute_dofs(fe_stretch);

    // First those related to the stretch...
    Vector<double> stretch, local_stretch_qp, local_stretch_dof;
    stretch.reinit(dof_handler_stretch.n_dofs());
    local_stretch_qp.reinit(n_q_points);
    local_stretch_dof.reinit(fe_stretch.n_dofs_per_cell());

    // Then those related to the orientation vector.
    // According to step-18, we can only project scalar
    // quantities, therefore we store the orientation
    // as a "vector of Vectors", which we can then
    // attach to a DataOut object component by component.
    std::vector<Vector<double>> 
    orientation(dim, Vector<double>()),
    local_orientation_qp(dim, Vector<double>()), 
    local_orientation_dof(dim, Vector<double>());
    for (unsigned int i = 0; i < dim; ++i)
    {
      orientation[i].reinit(dof_handler_stretch.n_dofs());
      local_orientation_qp[i].reinit(n_q_points);
      local_orientation_dof[i].reinit(fe_stretch.n_dofs_per_cell());
    }

    // Compute the projection matrix first
    FullMatrix<double> qp_to_dof_matrix(fe_stretch.dofs_per_cell, n_q_points);

    FETools::compute_projection_from_quadrature_points_matrix(fe_stretch,
                                                              qf_cell,
                                                              qf_cell,
                                                              qp_to_dof_matrix);

    // Then compute the projection (cell-wise)
    for (const auto &cell : dof_handler_stretch.active_cell_iterators())
    {
      const std::vector<std::shared_ptr<const PointHistory<dim>>> 
      lqph = quadrature_point_history.get_data(cell);

      for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
      {
        const double                     lambda = lqph[q_point]->get_stretch();
        local_stretch_qp[q_point] = lambda;

        const Tensor<1, dim> orientation_vector = lqph[q_point]->get_orientation();
        for (unsigned int i = 0; i < dim; ++i)
          local_orientation_qp[i](q_point) = orientation_vector[i];
      }

      qp_to_dof_matrix.vmult(local_stretch_dof, local_stretch_qp);
      cell->set_dof_values(local_stretch_dof, stretch);

      for (unsigned int i = 0; i < dim; ++i)
      {
        qp_to_dof_matrix.vmult(local_orientation_dof[i], local_orientation_qp[i]);
        cell->set_dof_values(local_orientation_dof[i], orientation[i]);
      }
    }

    // Store values
    DataOut<dim> data_out;
    data_out.attach_dof_handler(dof_handler_stretch);
    data_out.add_data_vector(stretch, "stretch");
    data_out.add_data_vector(orientation[0],"orientation_x");
    data_out.add_data_vector(orientation[1],"orientation_y");
    data_out.add_data_vector(orientation[2],"orientation_z");
    
    Vector<double> soln(solution_n.size());
    for (unsigned int i = 0; i < soln.size(); ++i)
      soln(i) = solution_n(i);
    MappingQEulerian<dim> q_mapping(degree, dof_handler, soln);
    data_out.build_patches(q_mapping, degree);

    std::ostringstream filename;
    filename << save_dir << "/stretch-" << dim << "d-" 
             << std::setfill('0') << std::setw(3) << time.get_timestep() << ".vtu";
    
    std::ofstream output(filename.str().c_str());
    data_out.write_vtu(output);
  }
} // namespace Flexodeal


// @sect3{Main function}
// Lastly we provide the main driver function which appears
// no different to the other tutorials.
int main()
{
  using namespace Flexodeal;

  try
    {
      const unsigned int dim = 3;
      Solid<dim>         solid("parameters.prm");
      solid.run();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;

      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }

  return 0;
}
