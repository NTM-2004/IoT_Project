package com.iot.parking.repository;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import com.iot.parking.model.Gate;

@Repository
public interface GateRepository extends JpaRepository<Gate, Long>{}
