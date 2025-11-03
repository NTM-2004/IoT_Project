package com.iot.parking.repository;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import com.iot.parking.model.Plate;

@Repository
public interface PlateRepository extends JpaRepository<Plate, Integer> {}
